#include "vulkanrenderer.h"

#include "externals/scope_guard/scope_guard.hpp"
#include "utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#define GLM_FORCE_INTRINSICS
#define GLM_FORCE_EXPLICIT_CTOR
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <QColorSpace>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>

namespace {
const QString vertShaderName = QStringLiteral(":/shaders/shader.vert.spv");
const QString fragShaderName = QStringLiteral(":/shaders/shader.frag.spv");
const QString textureName = QStringLiteral(":/textures/texture.jpg");

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

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;

    [[nodiscard]] static constexpr VkVertexInputBindingDescription createBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    [[nodiscard]] static constexpr std::array<VkVertexInputAttributeDescription, 3> createAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VkFormat::VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
};

const std::array vertices{
    Vertex{{-0.5F, -0.5F, 0.0F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}},
    Vertex{{0.5F, -0.5F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}},
    Vertex{{0.5F, 0.5F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
    Vertex{{-0.5F, 0.5F, 0.0F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F}},

    Vertex{{-0.5F, -0.5F, -0.5F}, {1.0F, 0.0F, 0.0F}, {1.0F, 0.0F}},
    Vertex{{0.5F, -0.5F, -0.5F}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}},
    Vertex{{0.5F, 0.5F, -0.5F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
    Vertex{{-0.5F, 0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}, {1.0F, 1.0F}}
};

constexpr std::array<uint16_t, 12> indices{
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4
};

[[noreturn]] void throwErrorMessage(VkResult actualResult, const char *errorMessage, VkResult expectedResult)
{
    std::string message(errorMessage);
    message += "expected result: ";
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

struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
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
    m_window->setPhysicalDeviceIndex(0);
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
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();
    QVulkanWindowRenderer::initSwapChainResources();
}

void VulkanRenderer::releaseSwapChainResources()
{
    qDebug() << "releaseSwapChainResources";
    for (auto iBuffer : qAsConst(m_uniformBuffers)) {
        destroyBufferWithMemory(iBuffer);
    }
    m_devFuncs->vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    m_devFuncs->vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
    m_devFuncs->vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    QVulkanWindowRenderer::releaseSwapChainResources();
}

void VulkanRenderer::releaseResources()
{
    qDebug() << "releaseResources";
    destroyBufferWithMemory(m_indexBuffer);
    destroyBufferWithMemory(m_vertexBuffer);
    m_devFuncs->vkDestroySampler(m_device, m_textureSampler, nullptr);
    m_devFuncs->vkDestroyImageView(m_device, m_textureImageView, nullptr);
    destroyImageWithMemory(m_textureImage);
    m_devFuncs->vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
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

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    VkDescriptorSetLayoutBinding &uboLayoutBinding = bindings[0];
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutBinding &samplerLayoutBinding = bindings[1];
    samplerLayoutBinding.binding = 1;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;

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

    auto bindingDescription = Vertex::createBindingDescription();
    auto attributeDescriptions = Vertex::createAttributeDescriptions();

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
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = sampleCountFlagBits;
    multisampling.minSampleShading = 1.0F;
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

    VkDeviceSize bufferSize = sizeof(vertices);

    auto stagingBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex());
    auto bufferGuard = sg::make_scope_guard([&, this]{ destroyBufferWithMemory(stagingBuffer); });

    void *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBuffer.memory, 0, bufferSize, {}, &data),
                  "failed to map memory to staging vertex buffer");
    {
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, stagingBuffer.memory); });
        std::copy(vertices.cbegin(), vertices.cend(), reinterpret_cast<Vertex *>(data));
    }

    m_vertexBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  m_window->deviceLocalMemoryIndex());

    copyBuffer(stagingBuffer.buffer, m_vertexBuffer.buffer, bufferSize);
}

void VulkanRenderer::createIndexBuffer()
{
    qDebug() << "Create index buffer";

    VkDeviceSize bufferSize = sizeof(indices);

    auto stagingBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex());
    auto bufferGuard = sg::make_scope_guard([&, this]{ destroyBufferWithMemory(stagingBuffer); });

    void *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBuffer.memory, 0, bufferSize, {}, &data),
                  "failed to map memory to staging index buffer");
    {
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, stagingBuffer.memory); });
        std::copy(indices.cbegin(), indices.cend(), reinterpret_cast<uint16_t *>(data));
    }

    m_indexBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                 m_window->deviceLocalMemoryIndex());

    copyBuffer(stagingBuffer.buffer, m_indexBuffer.buffer, bufferSize);
}

void VulkanRenderer::createUniformBuffers()
{
    qDebug() << "Create uniform buffers";

    auto swapChainImageCount = m_window->swapChainImageCount();
    m_uniformBuffers.clear();
    m_uniformBuffers.reserve(swapChainImageCount);
    for (int i = 0; i < swapChainImageCount; ++i) {
        m_uniformBuffers.append(createBuffer(sizeof(UniformBufferObject), VkBufferUsageFlagBits::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_window->hostVisibleMemoryIndex()));
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
    m_devFuncs->vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT16);

    m_devFuncs->vkCmdDrawIndexed(commandBuffer, indices.size(), 1, 0, 0, 0);
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
    auto ratio = static_cast<float>(swapChainImageSize.width()) / static_cast<float>(swapChainImageSize.height());

    VkDeviceMemory uniformBufferMemory = m_uniformBuffers.at(m_window->currentSwapChainImageIndex()).memory;

    void *data{};

    checkVkResult(m_devFuncs->vkMapMemory(m_device, uniformBufferMemory, 0, sizeof(UniformBufferObject), {}, &data),
                  "failed to map uniform buffer object memory");
    auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, uniformBufferMemory); });

    UniformBufferObject &ubo = *reinterpret_cast<UniformBufferObject *>(data);
    ubo.model = glm::rotate(glm::mat4{1.0F}, time * glm::radians(90.0F), glm::vec3{0.0F, 0.0F, 1.0F});
    ubo.view = glm::lookAt(glm::vec3{2.0F, 2.0F, 2.0F}, glm::vec3{0.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 0.0F, 1.0F});
    ubo.proj = glm::perspective(glm::radians(45.0F), ratio, 0.1F, 10.0F);
    ubo.proj[1][1] = -ubo.proj[1][1];
}

void VulkanRenderer::createDescriptorPool()
{
    qDebug() << "Create descriptor pool";
    auto swapChainImageCount = m_window->swapChainImageCount();

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = swapChainImageCount;
    poolSizes[1].type = VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = swapChainImageCount;

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
        const auto *iUniformBuffers = m_uniformBuffers.cbegin();
        const auto *iDescriptorSets = m_descriptorSets.cbegin();
        for (; iUniformBuffers != m_uniformBuffers.cend() && iDescriptorSets != m_descriptorSets.cend(); ++iUniformBuffers, ++iDescriptorSets) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = iUniformBuffers->buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(UniformBufferObject);

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = m_textureImageView;
            imageInfo.sampler = m_textureSampler;

            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

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
                .convertToFormat(QImage::Format::Format_RGBX8888);
        texture.convertToColorSpace(QColorSpace::NamedColorSpace::SRgb);
        if (texture.isNull()) {
            throw std::runtime_error("failed to load texture image");
        }
        VkDeviceSize imageSize = texture.sizeInBytes();
        texWidth = texture.width();
        texHeight = texture.height();

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

    m_textureImage = createImage(texWidth, texHeight, textureFormat, VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
                     VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_DST_BIT | VkImageUsageFlagBits::VK_IMAGE_USAGE_SAMPLED_BIT,
                     m_window->deviceLocalMemoryIndex());
    transitionImageLayout(m_textureImage.image, textureFormat, VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer.buffer, m_textureImage.image, texWidth, texHeight);
    transitionImageLayout(m_textureImage.image, textureFormat, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

ImageWithMemory VulkanRenderer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, uint32_t memoryTypeIndex) const
{
    qDebug() << "Create image";

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VkImageType::VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
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

void VulkanRenderer::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) const
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
    barrier.subresourceRange.levelCount = 1;
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
    m_textureImageView = createImageView(m_textureImage.image, textureFormat);
}

VkImageView VulkanRenderer::createImageView(VkImage image, VkFormat format) const
{
    qDebug() << "Create image view";
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VkImageViewType::VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
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
    samplerInfo.maxLod = 0.0F;
    checkVkResult(m_devFuncs->vkCreateSampler(m_device, &samplerInfo, nullptr, &m_textureSampler),
                  "failed to create texture sampler");
}

void VulkanRenderer::createDepthResources()
{
    qDebug() << "Create depth resources";
    transitionImageLayout(m_window->depthStencilImage(), m_window->depthStencilFormat(),
                          VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED, VkImageLayout::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
}

void VulkanRenderer::destroyBufferWithMemory(const BufferWithMemory &buffer) const
{
    qDebug() << "Destroy buffer with memory";
    m_devFuncs->vkDestroyBuffer(m_device, buffer.buffer, nullptr);
    m_devFuncs->vkFreeMemory(m_device, buffer.memory, nullptr);
}

void VulkanRenderer::destroyImageWithMemory(const ImageWithMemory &image) const
{
    qDebug() << "Destroy image with memory";
    m_devFuncs->vkDestroyImage(m_device, image.image, nullptr);
    m_devFuncs->vkFreeMemory(m_device, image.memory, nullptr);
}
