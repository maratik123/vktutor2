#include "vulkanrenderer.h"

#include "externals/scope_guard/scope_guard.hpp"

#include "utils.h"
#include "glm.h"
#include "model.h"
#include "vertex.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>

#include <QColorSpace>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>

namespace {
const QString vertShaderName = QStringLiteral(":/shaders/shader.vert.spv");
const QString fragShaderName = QStringLiteral(":/shaders/shader.frag.spv");
const QString textureName = QStringLiteral(":/textures/viking_room.png");
const QString modelDirName = QStringLiteral(":/models");
const QString modelName = QStringLiteral("viking_room.obj");

[[nodiscard]] QByteArray readFile(const QString &fileName)
{
    qDebug() << "readFile: " << fileName;
    QFile file(fileName);
    if (!file.open(QIODevice::OpenModeFlag::ReadOnly)) {
        qDebug() << "file not found: " << fileName;
        throw std::runtime_error("file not found");
    }
    return file.readAll();
}

constexpr VkClearColorValue clearColor{{0.0F, 0.0F, 0.0F, 1.0F}};
constexpr VkClearDepthStencilValue clearDepthStencil{1.0F, 0};

[[nodiscard]] constexpr std::array<VkClearValue, 3> createClearValues()
{
    std::array<VkClearValue, 3> clearValue{};
    clearValue[0].color = clearColor;
    clearValue[1].depthStencil = clearDepthStencil;
    clearValue[2].color = clearColor;
    return clearValue;
}

[[nodiscard]] constexpr VkRect2D createVkRect2D(const QSize &rect)
{
    return {
        VkOffset2D{0, 0},
        VkExtent2D{
            static_cast<uint32_t>(rect.width()),
            static_cast<uint32_t>(rect.height())
        }
    };
}

[[noreturn]] void throwErrorMessage(VkResult actualResult, const char *errorMessage, VkResult expectedResult)
{
    std::string message(errorMessage);
    message += ", expected result: ";
    message += std::to_string(expectedResult);
    message += ", actual result: ";
    message += std::to_string(actualResult);
    throw std::runtime_error(message);
}

void checkVkResult(VkResult actualResult, const char *errorMessage, VkResult expectedResult = VkResult::VK_SUCCESS)
{
    if (Q_LIKELY(actualResult == expectedResult)) {
        return;
    }
    throwErrorMessage(actualResult, errorMessage, expectedResult);
}

struct VertBindingObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 modelInvTrans;
    alignas(16) glm::mat4 projView;
};

struct FragBindingObject {
    alignas(16) glm::vec4 ambientColor;
    alignas(16) glm::vec4 diffuseLightPos;
    alignas(16) glm::vec4 diffuseLightColor;
};

constexpr VkFormat textureFormat = VkFormat::VK_FORMAT_R8G8B8A8_SRGB;

constexpr bool hasStencilComponent(VkFormat format)
{
    return format == VkFormat::VK_FORMAT_D32_SFLOAT_S8_UINT || format == VkFormat::VK_FORMAT_D24_UNORM_S8_UINT;
}

constexpr VkImageAspectFlags evalAspectFlags(VkImageLayout newLayout, VkFormat format)
{
    if (newLayout != VkImageLayout::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        return VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
    }
    if (!hasStencilComponent(format)) {
        return VkImageAspectFlagBits::VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    return VkImageAspectFlagBits::VK_IMAGE_ASPECT_DEPTH_BIT | VkImageAspectFlagBits::VK_IMAGE_ASPECT_STENCIL_BIT;
}
}

VulkanRenderer::VulkanRenderer(QVulkanWindow *w)
    : m_window{w}
    , m_vkInst{m_window->vulkanInstance()}
    , m_funcs{m_vkInst->functions()}
    , m_physDevice{}
    , m_device{}
    , m_devFuncs{}
    , m_msaa{}
    , m_descriptorSetLayout{}
    , m_pipelineLayout{}
    , m_graphicsPipeline{}
    , m_vertexBuffer{}
    , m_indexBuffer{}
    , m_descriptorPool{}
    , m_textureImage{}
    , m_textureImageView{}
    , m_textureSampler{}
    , m_mipLevels{}
{
    qDebug() << "Create vulkan renderer";
}

void VulkanRenderer::preInitResources()
{
    qDebug() << "preInitResources";
    m_window->setPreferredColorFormats({
            VkFormat::VK_FORMAT_B8G8R8A8_SRGB,
            VkFormat::VK_FORMAT_B8G8R8A8_UNORM
    });
    loadModel();
    m_window->setPhysicalDeviceIndex(0);
    {
        auto supportedSampleCounts = m_window->supportedSampleCounts();
        m_window->setSampleCount(supportedSampleCounts.constLast());
    }
    QVulkanWindowRenderer::preInitResources();
}

void VulkanRenderer::initResources()
{
    qDebug() << "initResources";
    m_physDevice = m_window->physicalDevice();
    m_device = m_window->device();
    m_devFuncs = m_vkInst->deviceFunctions(m_device);
    createDescriptorSetLayout();
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
    createVertexBuffer();
    createIndexBuffer();
    QVulkanWindowRenderer::initResources();
}

void VulkanRenderer::initSwapChainResources()
{
    qDebug() << "initSwapChainResources";
    createGraphicsPipeline();
    createDepthResources();
    createVertUniformBuffers();
    createFragUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    QVulkanWindowRenderer::initSwapChainResources();
}

void VulkanRenderer::releaseSwapChainResources()
{
    qDebug() << "releaseSwapChainResources";
    destroyUniformBuffers(m_vertUniformBuffers);
    destroyUniformBuffers(m_fragUniformBuffers);
    m_devFuncs->vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    m_descriptorPool = {};
    m_devFuncs->vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
    m_graphicsPipeline = {};
    m_devFuncs->vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    m_pipelineLayout = {};
    QVulkanWindowRenderer::releaseSwapChainResources();
}

void VulkanRenderer::releaseResources()
{
    qDebug() << "releaseResources";
    destroyBufferWithMemory(m_indexBuffer);
    destroyBufferWithMemory(m_vertexBuffer);
    m_devFuncs->vkDestroySampler(m_device, m_textureSampler, nullptr);
    m_textureSampler = {};
    m_devFuncs->vkDestroyImageView(m_device, m_textureImageView, nullptr);
    m_textureImageView = {};
    destroyImageWithMemory(m_textureImage);
    m_devFuncs->vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    m_descriptorSetLayout = {};
    m_devFuncs = {};
    m_device = {};
    m_physDevice = {};
    QVulkanWindowRenderer::releaseResources();
}

void VulkanRenderer::physicalDeviceLost()
{
    qDebug() << "physicalDeviceLost";
    QVulkanWindowRenderer::physicalDeviceLost();
}

void VulkanRenderer::logicalDeviceLost()
{
    qDebug() << "logicalDeviceLost";
    QVulkanWindowRenderer::logicalDeviceLost();
}

VkShaderModule VulkanRenderer::createShaderModule(const QByteArray &code) const
{
    qDebug() << "create shader module";
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.constData());
    VkShaderModule shaderModule{};
    checkVkResult(m_devFuncs->vkCreateShaderModule(m_window->device(), &createInfo, nullptr, &shaderModule),
                  "failed to create shader module");
    return shaderModule;
}

void VulkanRenderer::createDescriptorSetLayout()
{
    qDebug() << "Create descriptor set layout";

    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    VkDescriptorSetLayoutBinding &uboLayoutBinding = bindings[0];
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding &samplerLayoutBinding = bindings[1];
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.stageFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
    samplerLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding &lightInfoLayoutBinding = bindings[2];
    lightInfoLayoutBinding.binding = 2;
    lightInfoLayoutBinding.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightInfoLayoutBinding.descriptorCount = 1;
    lightInfoLayoutBinding.stageFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
    lightInfoLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();
    checkVkResult(m_devFuncs->vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout),
                  "failed to create descriptor set layout");
}

void VulkanRenderer::createGraphicsPipeline()
{
    qDebug() << "Create graphics pipeline";
    VkShaderModule vertShaderModule{};
    {
        auto vertShaderCode = readFile(vertShaderName);
        vertShaderModule = createShaderModule(vertShaderCode);
    }
    auto vertGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkDestroyShaderModule(m_device, vertShaderModule, nullptr); });

    VkShaderModule fragShaderModule{};
    {
        auto fragShaderCode = readFile(fragShaderName);
        fragShaderModule = createShaderModule(fragShaderCode);
    }
    auto fragGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkDestroyShaderModule(m_device, fragShaderModule, nullptr); });

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkPipelineShaderStageCreateInfo &vertShaderStageInfo = shaderStages[0];
    vertShaderStageInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo &fragShaderStageInfo = shaderStages[1];
    fragShaderStageInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    auto bindingDescription = ModelVertex::createBindingDescription();
    auto attributeDescriptions = ModelVertex::createAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptions.size();
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VkPrimitiveTopology::VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    auto swapChainImageSize = m_window->swapChainImageSize();

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(swapChainImageSize.width());
    viewport.height = static_cast<float>(swapChainImageSize.height());
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;

    VkRect2D scissor = createVkRect2D(swapChainImageSize);

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VkPolygonMode::VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0F;
    rasterizer.cullMode = VkCullModeFlagBits::VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VkFrontFace::VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0F;
    rasterizer.depthBiasClamp = 0.0F;
    rasterizer.depthBiasSlopeFactor = 0.0F;

    auto sampleCountFlagBits = m_window->sampleCountFlagBits();

    m_msaa = sampleCountFlagBits > VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
    qDebug() << "MSAA: " << m_msaa;
    qDebug() << "sampleCountFlagBits: " << sampleCountFlagBits;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples = sampleCountFlagBits;
    multisampling.minSampleShading = 0.2F;
    multisampling.pSampleMask = nullptr;
    multisampling.alphaToCoverageEnable = VK_FALSE;
    multisampling.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VkCompareOp::VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0F;
    depthStencil.maxDepthBounds = 1.0F;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
            VkColorComponentFlags{}
            | VkColorComponentFlagBits::VK_COLOR_COMPONENT_B_BIT
            | VkColorComponentFlagBits::VK_COLOR_COMPONENT_G_BIT
            | VkColorComponentFlagBits::VK_COLOR_COMPONENT_R_BIT
            | VkColorComponentFlagBits::VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstColorBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.colorBlendOp = VkBlendOp::VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VkBlendFactor::VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VkBlendOp::VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VkLogicOp::VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    std::fill(std::begin(colorBlending.blendConstants), std::end(colorBlending.blendConstants), 0.0F);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;
    checkVkResult(m_devFuncs->vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout),
                  "failed to create pipeline layout");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = shaderStages.size();
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_window->defaultRenderPass();
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    checkVkResult(m_devFuncs->vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline),
                  "failed to create graphics pipeline");
}

void VulkanRenderer::createVertexBuffer()
{
    qDebug() << "Create vertex buffer";

    VkDeviceSize bufferSize = m_vertices.size() * sizeof(decltype(m_vertices)::value_type);

    auto stagingBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex());
    auto bufferGuard = sg::make_scope_guard([&, this]{ destroyBufferWithMemory(stagingBuffer); });

    void *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBuffer.memory, 0, bufferSize, {}, &data),
                  "failed to map memory to staging vertex buffer");
    {
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, stagingBuffer.memory); });
        std::copy(m_vertices.cbegin(), m_vertices.cend(), reinterpret_cast<decltype(m_vertices)::value_type *>(data));
    }

    m_vertexBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  m_window->deviceLocalMemoryIndex());

    copyBuffer(stagingBuffer.buffer, m_vertexBuffer.buffer, bufferSize);
}

void VulkanRenderer::createIndexBuffer()
{
    qDebug() << "Create index buffer";

    VkDeviceSize bufferSize = m_indices.size() * sizeof(decltype(m_indices)::value_type);

    auto stagingBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex());
    auto bufferGuard = sg::make_scope_guard([&, this]{ destroyBufferWithMemory(stagingBuffer); });

    void *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBuffer.memory, 0, bufferSize, {}, &data),
                  "failed to map memory to staging index buffer");
    {
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, stagingBuffer.memory); });
        std::copy(m_indices.cbegin(), m_indices.cend(), reinterpret_cast<decltype(m_indices)::value_type *>(data));
    }

    m_indexBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 m_window->deviceLocalMemoryIndex());

    copyBuffer(stagingBuffer.buffer, m_indexBuffer.buffer, bufferSize);
}

void VulkanRenderer::createVertUniformBuffers()
{
    qDebug() << "Create vertex uniform buffers";

    createUniformBuffers<VertBindingObject>(m_vertUniformBuffers);
}

void VulkanRenderer::createFragUniformBuffers()
{
    qDebug() << "Create fragment uniform buffers";

    createUniformBuffers<FragBindingObject>(m_fragUniformBuffers);
}

template<typename T>
void VulkanRenderer::createUniformBuffers(QVector<BufferWithMemory> &buffers) const
{
    createUniformBuffers(buffers, sizeof(T));
}

void VulkanRenderer::createUniformBuffers(QVector<BufferWithMemory> &buffers, std::size_t size) const
{
    qDebug() << "Create uniform buffers";

    auto swapChainImageCount = m_window->swapChainImageCount();
    buffers.clear();
    buffers.reserve(swapChainImageCount);
    for (int i = 0; i < swapChainImageCount; ++i) {
        buffers << createBuffer(size, VkBufferUsageFlagBits::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_window->hostVisibleMemoryIndex());
    }
}


BufferWithMemory VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryTypeIndex) const
{
    qDebug() << "Create buffer";

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buffer{};
    checkVkResult(m_devFuncs->vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer),
                  "failed to create buffer");

    VkMemoryRequirements memRequirements{};
    m_devFuncs->vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    VkDeviceMemory memory{};
    checkVkResult(m_devFuncs->vkAllocateMemory(m_device, &allocInfo, nullptr, &memory),
                  "failed to allocate buffer memory");
    checkVkResult(m_devFuncs->vkBindBufferMemory(m_device, buffer, memory, 0),
                  "failed to bind vertex buffer to memory");
    return {buffer, memory};
}

void VulkanRenderer::startNextFrame()
{
    updateUniformBuffer();
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_window->defaultRenderPass();
    renderPassInfo.framebuffer = m_window->currentFramebuffer();
    renderPassInfo.renderArea = createVkRect2D(m_window->swapChainImageSize());
    auto clearValues = createClearValues();
    renderPassInfo.clearValueCount = m_msaa ? 3 : 2;
    renderPassInfo.pClearValues = clearValues.data();
    VkCommandBuffer commandBuffer = m_window->currentCommandBuffer();
    m_devFuncs->vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VkSubpassContents::VK_SUBPASS_CONTENTS_INLINE);
    m_devFuncs->vkCmdBindPipeline(commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    std::array vertexBuffers{m_vertexBuffer.buffer};
    std::array offsets{static_cast<VkDeviceSize>(0)};
    m_devFuncs->vkCmdBindVertexBuffers(commandBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets.data());

    m_devFuncs->vkCmdBindDescriptorSets(commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0,
                                        1, &m_descriptorSets.at(m_window->currentSwapChainImageIndex()), 0, nullptr);
    m_devFuncs->vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);

    m_devFuncs->vkCmdDrawIndexed(commandBuffer, m_indices.size(), 1, 0, 0, 0);
    m_devFuncs->vkCmdEndRenderPass(commandBuffer);
    m_window->frameReady();
    m_window->requestUpdate();
}

void VulkanRenderer::updateUniformBuffer() const
{
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    auto swapChainImageSize = m_window->swapChainImageSize();

    auto currentSwapChainImageIndex = m_window->currentSwapChainImageIndex();

    {
        VkDeviceMemory vertUniformBufferMemory = m_vertUniformBuffers.at(currentSwapChainImageIndex).memory;

        void *data{};

        checkVkResult(m_devFuncs->vkMapMemory(m_device, vertUniformBufferMemory, 0, sizeof(VertBindingObject), {}, &data),
                      "failed to map uniform buffer object memory");
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, vertUniformBufferMemory); });

        VertBindingObject &vertUbo = *reinterpret_cast<VertBindingObject *>(data);

        vertUbo.model = glm::rotate(glm::mat4{1.0F}, time * glm::radians(6.0F), glm::vec3{0.0F, 0.0F, 1.0F});

        vertUbo.modelInvTrans = glm::transpose(glm::inverse(vertUbo.model));

        auto ratio = static_cast<float>(swapChainImageSize.width()) / static_cast<float>(swapChainImageSize.height());
        vertUbo.projView = glm::perspective(glm::radians(45.0F), ratio, 0.1F, 10.0F);
        vertUbo.projView[1][1] = -vertUbo.projView[1][1];

        glm::mat4 view = glm::lookAt(glm::vec3{2.0F, 2.0F, 2.0F}, glm::vec3{0.0F, 0.0F, 0.25F}, glm::vec3{0.0F, 0.0F, 1.0F});

        vertUbo.projView *= view;
    }
    {
        VkDeviceMemory fragUniformBufferMemory = m_fragUniformBuffers.at(currentSwapChainImageIndex).memory;

        void *data{};

        checkVkResult(m_devFuncs->vkMapMemory(m_device, fragUniformBufferMemory, 0, sizeof(FragBindingObject), {}, &data),
                      "failed to map light info memory");
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, fragUniformBufferMemory); });

        FragBindingObject &fragUbo = *reinterpret_cast<FragBindingObject *>(data);

        glm::mat4 modelDiffuseLightPos = glm::rotate(glm::mat4{1.0F}, -time * glm::radians(30.0F), glm::vec3{0.0F, 0.0F, 1.0F});

        fragUbo.ambientColor = {0.1F, 0.1F, 0.1F, 1.0F};
        fragUbo.diffuseLightPos = modelDiffuseLightPos * glm::vec4(-2.0F, 2.0F, 1.0F, 1.0F);
        fragUbo.diffuseLightColor = {1.0F, 1.0F, 0.0F, 1.0F};
    }
}

void VulkanRenderer::createDescriptorPool()
{
    qDebug() << "Create descriptor pool";
    auto swapChainImageCount = m_window->swapChainImageCount();

    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = swapChainImageCount;
    poolSizes[1].type = VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = swapChainImageCount;
    poolSizes[2].type = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[2].descriptorCount = swapChainImageCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = swapChainImageCount;
    checkVkResult(m_devFuncs->vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool),
                  "failed to create descriptor pool");
}

void VulkanRenderer::createDescriptorSets()
{
    qDebug() << "Create descriptors sets";
    auto swapChainImageCount = m_window->swapChainImageCount();
    QVector<VkDescriptorSetLayout> layouts{swapChainImageCount, m_descriptorSetLayout};
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = swapChainImageCount;
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(swapChainImageCount);
    checkVkResult(m_devFuncs->vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()),
                  "failed to allocate descriptor sets");

    {
        const auto *iVertUniformBuffers = m_vertUniformBuffers.cbegin();
        const auto *iFragUniformBuffers = m_fragUniformBuffers.cbegin();
        const auto *iDescriptorSets = m_descriptorSets.cbegin();
        for (; iVertUniformBuffers != m_vertUniformBuffers.cend()
             && iFragUniformBuffers != m_fragUniformBuffers.cend()
             && iDescriptorSets != m_descriptorSets.cend();
             ++iVertUniformBuffers, ++iFragUniformBuffers, ++iDescriptorSets) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = iVertUniformBuffers->buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(VertBindingObject);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = m_textureImageView;
            imageInfo.sampler = m_textureSampler;

            VkDescriptorBufferInfo lightInfoBufferInfo{};
            lightInfoBufferInfo.buffer = iFragUniformBuffers->buffer;
            lightInfoBufferInfo.offset = 0;
            lightInfoBufferInfo.range = sizeof(FragBindingObject);

            std::array<VkWriteDescriptorSet, 3> descriptorWrites{};

            descriptorWrites[0].sType = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = *iDescriptorSets;
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pBufferInfo = &bufferInfo;

            descriptorWrites[1].sType = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = *iDescriptorSets;
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo;

            descriptorWrites[2].sType = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[2].dstSet = *iDescriptorSets;
            descriptorWrites[2].dstBinding = 2;
            descriptorWrites[2].dstArrayElement = 0;
            descriptorWrites[2].descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[2].descriptorCount = 1;
            descriptorWrites[2].pBufferInfo = &lightInfoBufferInfo;

            m_devFuncs->vkUpdateDescriptorSets(m_device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
    }
}

void VulkanRenderer::createTextureImage()
{
    qDebug() << "Create texture image";
    BufferWithMemory stagingBuffer{};
    int texWidth{};
    int texHeight{};
    try {
        QImage texture = QImage{textureName}
                .convertToFormat(QImage::Format::Format_RGBA8888);
        texture.convertToColorSpace(QColorSpace::NamedColorSpace::SRgb);
        if (texture.isNull()) {
            throw std::runtime_error("failed to load texture image");
        }
        VkDeviceSize imageSize = texture.sizeInBytes();
        texWidth = texture.width();
        texHeight = texture.height();
        m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

        stagingBuffer = createBuffer(imageSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex());

        void *data{};
        checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBuffer.memory, 0, imageSize, {}, &data),
                      "failed to map texture staging buffer memory");
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, stagingBuffer.memory); });
        std::copy_n(static_cast<const uint8_t *>(texture.constBits()), imageSize, static_cast<uint8_t *>(data));
    } catch (...) {
        destroyBufferWithMemory(stagingBuffer);
        throw;
    }

    auto bufferGuard = sg::make_scope_guard([&, this]{ destroyBufferWithMemory(stagingBuffer); });

    m_textureImage = createImage(texWidth, texHeight, m_mipLevels, VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT,
                                 textureFormat, VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
                                 static_cast<VkImageUsageFlags>(VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                                 | static_cast<VkImageUsageFlags>(VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                                 | static_cast<VkImageUsageFlags>(VkImageUsageFlagBits::VK_IMAGE_USAGE_SAMPLED_BIT),
                                 m_window->deviceLocalMemoryIndex());
    transitionImageLayout(m_textureImage.image, textureFormat, VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_mipLevels);
    copyBufferToImage(stagingBuffer.buffer, m_textureImage.image, texWidth, texHeight);
    generateMipmaps(m_textureImage.image, textureFormat, texWidth, texHeight, m_mipLevels);
}

void VulkanRenderer::generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const
{
    qDebug() << "Generate mipmaps for levels: " << mipLevels;
    VkFormatProperties formatProperties{};
    m_funcs->vkGetPhysicalDeviceFormatProperties(m_physDevice, imageFormat, &formatProperties);
    VkFormatFeatureFlags expectedFeatures = VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((formatProperties.optimalTilingFeatures & expectedFeatures) != expectedFeatures) {
        throw std::runtime_error("texture image format does not support linear blitting");
    }
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    auto bufferGuard = sg::make_scope_guard([&, this]{ endSingleTimeCommands(commandBuffer); });

    VkImageMemoryBarrier barrier{};
    barrier.sType = VkStructureType::VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    VkImageBlit blit{};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcSubresource.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.baseArrayLayer = 0;
    blit.srcSubresource.layerCount = 1;
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstSubresource.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.baseArrayLayer = 0;
    blit.dstSubresource.layerCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; ++i) {
        qDebug() << "Generating level: " << i << ", mipWidth: " << mipWidth << ", mipHeight: " << mipHeight;
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_READ_BIT;

        m_devFuncs->vkCmdPipelineBarrier(commandBuffer,
                                         VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         {},
                                         0, nullptr,
                                         0, nullptr,
                                         1, &barrier);

        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.mipLevel = i - 1;
        blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
        blit.dstSubresource.mipLevel = i;

        m_devFuncs->vkCmdBlitImage(commandBuffer,
                                   image, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   image, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1, &blit,
                                   VkFilter::VK_FILTER_LINEAR);

        barrier.oldLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_SHADER_READ_BIT;

        m_devFuncs->vkCmdPipelineBarrier(commandBuffer,
                                         VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
                                         VkPipelineStageFlagBits::VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                         {},
                                         0, nullptr,
                                         0, nullptr,
                                         1, &barrier);
        if (mipWidth > 1) {
            mipWidth /= 2;
        }
        if (mipHeight > 1) {
            mipHeight /= 2;
        }
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_SHADER_READ_BIT;

    m_devFuncs->vkCmdPipelineBarrier(commandBuffer,
                                     VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     VkPipelineStageFlagBits::VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                     {},
                                     0, nullptr,
                                     0, nullptr,
                                     1, &barrier);
}

ImageWithMemory VulkanRenderer::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
                                            VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, uint32_t memoryTypeIndex) const
{
    qDebug() << "Create image";

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VkImageType::VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = numSamples;
    imageInfo.flags = {};
    VkImage image{};
    checkVkResult(m_devFuncs->vkCreateImage(m_device, &imageInfo, nullptr, &image),
                  "failed to create image");

    VkMemoryRequirements memRequirements{};
    m_devFuncs->vkGetImageMemoryRequirements(m_device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    VkDeviceMemory memory{};
    checkVkResult(m_devFuncs->vkAllocateMemory(m_device, &allocInfo, nullptr, &memory),
                  "failed to allocate image memory");

    checkVkResult(m_devFuncs->vkBindImageMemory(m_device, image, memory, 0),
                  "failed to bind image memory");
    return {image, memory};
}

void VulkanRenderer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const
{
    qDebug() << "Copy buffer";

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    auto bufferGuard = sg::make_scope_guard([&, this]{ endSingleTimeCommands(commandBuffer); });

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    m_devFuncs->vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
}

void VulkanRenderer::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) const
{
    qDebug() << "Transition image layout";

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    auto bufferGuard = sg::make_scope_guard([&, this]{ endSingleTimeCommands(commandBuffer); });

    VkImageMemoryBarrier barrier{};
    barrier.sType = VkStructureType::VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;

    barrier.subresourceRange.aspectMask = evalAspectFlags(newLayout, format);
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage{};
    VkPipelineStageFlags destinationStage{};

    if (oldLayout == VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VkImageLayout::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = {};
        barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VkAccessFlagBits::VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        throw std::runtime_error("unsupported layout transition");
    }

    m_devFuncs->vkCmdPipelineBarrier(
                commandBuffer,
                sourceStage, destinationStage,
                {},
                0, nullptr,
                0, nullptr,
                1, &barrier);
}

void VulkanRenderer::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const
{
    qDebug() << "Copy buffer to image";

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    auto bufferGuard = sg::make_scope_guard([&, this]{ endSingleTimeCommands(commandBuffer); });

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        width,
        height,
        1
    };
    m_devFuncs->vkCmdCopyBufferToImage(commandBuffer, buffer, image, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

VkCommandBuffer VulkanRenderer::beginSingleTimeCommands() const
{
    qDebug() << "Begin single time command";
    VkCommandPool commandPool = m_window->graphicsCommandPool();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer{};
    checkVkResult(m_devFuncs->vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer),
                  "failed to allocate command buffer for copy");
    try {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VkCommandBufferUsageFlagBits::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        checkVkResult(m_devFuncs->vkBeginCommandBuffer(commandBuffer, &beginInfo),
                      "failed to begin command buffer for copy");

        return commandBuffer;
    } catch(...) {
        m_devFuncs->vkFreeCommandBuffers(m_device, commandPool, 1, &commandBuffer);
        throw;
    }
}

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) const
{
    qDebug() << "End single time command";
    auto bufferGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkFreeCommandBuffers(m_device, m_window->graphicsCommandPool(), 1, &commandBuffer); });

    checkVkResult(m_devFuncs->vkEndCommandBuffer(commandBuffer),
                  "failed to end command buffer for copy");

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkQueue queue = m_window->graphicsQueue();
    checkVkResult(m_devFuncs->vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE),
                  "failed to submit command buffer for copy");
    checkVkResult(m_devFuncs->vkQueueWaitIdle(queue),
                  "failed to wait queue for copy");
}

void VulkanRenderer::createTextureImageView()
{
    qDebug() << "Create texture image view";
    m_textureImageView = createImageView(m_textureImage.image, textureFormat, m_mipLevels);
}

VkImageView VulkanRenderer::createImageView(VkImage image, VkFormat format, uint32_t mipLevels) const
{
    qDebug() << "Create image view";
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView{};
    checkVkResult(m_devFuncs->vkCreateImageView(m_device, &viewInfo, nullptr, &imageView),
                  "failed to create image view");
    return imageView;
}

void VulkanRenderer::createTextureSampler()
{
    qDebug() << "Create texture sampler";

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VkFilter::VK_FILTER_LINEAR;
    samplerInfo.minFilter = VkFilter::VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0F;
    samplerInfo.borderColor = VkBorderColor::VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VkCompareOp::VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0F;
    samplerInfo.minLod = 0.0F;
    samplerInfo.maxLod = static_cast<float>(m_mipLevels);
    checkVkResult(m_devFuncs->vkCreateSampler(m_device, &samplerInfo, nullptr, &m_textureSampler),
                  "failed to create texture sampler");
}

void VulkanRenderer::createDepthResources() const
{
    qDebug() << "Create depth resources";
    transitionImageLayout(m_window->depthStencilImage(), m_window->depthStencilFormat(),
                          VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED, VkImageLayout::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
}

void VulkanRenderer::loadModel()
{
    qDebug() << "Load model";
    auto model = Model::loadModel(modelDirName, modelName);
    m_vertices.swap(model.vertices);
    m_indices.swap(model.indices);
}

void VulkanRenderer::destroyBufferWithMemory(BufferWithMemory &buffer) const
{
    qDebug() << "Destroy buffer with memory";
    m_devFuncs->vkDestroyBuffer(m_device, buffer.buffer, nullptr);
    m_devFuncs->vkFreeMemory(m_device, buffer.memory, nullptr);
    buffer = {};
}

void VulkanRenderer::destroyImageWithMemory(ImageWithMemory &image) const
{
    qDebug() << "Destroy image with memory";
    m_devFuncs->vkDestroyImage(m_device, image.image, nullptr);
    m_devFuncs->vkFreeMemory(m_device, image.memory, nullptr);
    image = {};
}

void VulkanRenderer::destroyUniformBuffers(QVector<BufferWithMemory> &buffers) const
{
    qDebug() << "Destroy buffers";
    for (auto &iBuffer : buffers) {
        destroyBufferWithMemory(iBuffer);
    }
}
