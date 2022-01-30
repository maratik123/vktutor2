#include "vulkanrenderer.h"

#include "externals/scope_guard/scope_guard.hpp"
#include "utils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <sstream>

#include <QColorSpace>
#include <QDebug>
#include <QFile>
#include <QImage>
#include <QMatrix4x4>
#include <QVector2D>
#include <QVector3D>
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
    QVector2D pos;
    QVector3D color;

    [[nodiscard]] static constexpr VkVertexInputBindingDescription createBindingDescription()
    {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX;

        return bindingDescription;
    }

    [[nodiscard]] static constexpr std::array<VkVertexInputAttributeDescription, 2> createAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VkFormat::VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        return attributeDescriptions;
    }
};

constexpr std::array<Vertex, 4> vertices{
    Vertex{QVector2D{-0.5F, -0.5F}, QVector3D{1.0F, 0.0F, 0.0F}},
    Vertex{QVector2D{0.5F, -0.5F}, QVector3D{0.0F, 1.0F, 0.0F}},
    Vertex{QVector2D{0.5F, 0.5F}, QVector3D{0.0F, 0.0F, 1.0F}},
    Vertex{QVector2D{-0.5F, 0.5F}, QVector3D{1.0F, 1.0F, 1.0F}}
};

constexpr std::array<uint16_t, 6> indices{
    0, 1, 2, 2, 3, 0
};

void checkVkResult(VkResult actualResult, const char *errorMessage, VkResult expectedResult = VkResult::VK_SUCCESS)
{
    if (actualResult == expectedResult) {
        return;
    }
    std::ostringstream message(errorMessage, std::ios::ate);
    message << ": " << actualResult;
    throw std::runtime_error(message.str());
}

constexpr auto sizeOfQMatrix4x4InFloats = 4 * 4;
constexpr auto sizeOfQMatrix4x4InBytes = sizeof(float) * sizeOfQMatrix4x4InFloats;
constexpr auto sizeOfUniformBufferObject = sizeOfQMatrix4x4InBytes * 3;

struct UniformBufferObject {
    QMatrix4x4 model;
    QMatrix4x4 view;
    QMatrix4x4 proj;

    void copyDataTo(float *values) const {
        values = std::copy_n(model.constData(), sizeOfQMatrix4x4InFloats, values);
        values = std::copy_n(view.constData(), sizeOfQMatrix4x4InFloats, values);
        std::copy_n(proj.constData(), sizeOfQMatrix4x4InFloats, values);
    }
};
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
    QVulkanWindowRenderer::preInitResources();
}

void VulkanRenderer::initResources()
{
    qDebug() << "initResources";
    m_physDevice = m_window->physicalDevice();
    m_device = m_window->device();
    m_devFuncs = m_vkInst->deviceFunctions(m_device);
    createDescriptorSetLayout();
    QVulkanWindowRenderer::initResources();
}

void VulkanRenderer::initSwapChainResources()
{
    qDebug() << "initSwapChainResources";
    createGraphicsPipeline();
    createTextureImage();
    createVertexBuffer();
    createIndexBuffer();
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
    destroyBufferWithMemory(m_indexBuffer);
    destroyBufferWithMemory(m_vertexBuffer);
    destroyImageWithMemory(m_textureImage);
    m_devFuncs->vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
    m_devFuncs->vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    QVulkanWindowRenderer::releaseSwapChainResources();
}

void VulkanRenderer::releaseResources()
{
    qDebug() << "releaseResources";
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

    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;
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

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    std::array shaderStages{vertShaderStageInfo, fragShaderStageInfo};

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
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

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

    BufferWithMemory stagingBuffer{};
    createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex(), stagingBuffer);
    auto bufferGuard = sg::make_scope_guard([&, this]{ destroyBufferWithMemory(stagingBuffer); });

    void *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBuffer.memory, 0, bufferSize, {}, &data),
                  "failed to map memory to staging vertex buffer");
    {
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, stagingBuffer.memory); });
        std::copy(vertices.cbegin(), vertices.cend(), reinterpret_cast<Vertex *>(data));
    }

    createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 m_window->deviceLocalMemoryIndex(), m_vertexBuffer);

    copyBuffer(stagingBuffer.buffer, m_vertexBuffer.buffer, bufferSize);
}

void VulkanRenderer::createIndexBuffer()
{
    qDebug() << "Create index buffer";

    VkDeviceSize bufferSize = sizeof(indices);

    BufferWithMemory stagingBuffer{};
    createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex(), stagingBuffer);
    auto bufferGuard = sg::make_scope_guard([&, this]{ destroyBufferWithMemory(stagingBuffer); });

    void *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBuffer.memory, 0, bufferSize, {}, &data),
                  "failed to map memory to staging index buffer");
    {
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, stagingBuffer.memory); });
        std::copy(indices.cbegin(), indices.cend(), reinterpret_cast<uint16_t *>(data));
    }

    createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                 m_window->deviceLocalMemoryIndex(), m_indexBuffer);

    copyBuffer(stagingBuffer.buffer, m_indexBuffer.buffer, bufferSize);
}

void VulkanRenderer::createUniformBuffers()
{
    qDebug() << "Create uniform buffers";

    auto swapChainImageCount = m_window->swapChainImageCount();
    m_uniformBuffers.clear();
    m_uniformBuffers.reserve(swapChainImageCount);
    for (int i = 0; i < swapChainImageCount; ++i) {
        BufferWithMemory uniformBuffer{};
        createBuffer(sizeOfUniformBufferObject, VkBufferUsageFlagBits::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, m_window->hostVisibleMemoryIndex(), uniformBuffer);
        m_uniformBuffers.append(uniformBuffer);
    }
}

void VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryTypeIndex, BufferWithMemory &buffer) const
{
    qDebug() << "Create buffer";

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;

    checkVkResult(m_devFuncs->vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer.buffer),
                  "failed to create buffer");

    VkMemoryRequirements memRequirements{};
    m_devFuncs->vkGetBufferMemoryRequirements(m_device, buffer.buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    checkVkResult(m_devFuncs->vkAllocateMemory(m_device, &allocInfo, nullptr, &buffer.memory),
                  "failed to allocate buffer memory");
    checkVkResult(m_devFuncs->vkBindBufferMemory(m_device, buffer.buffer, buffer.memory, 0),
                  "failed to bind vertex buffer to memory");
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

    std::array<VkBuffer, 1> vertexBuffers{m_vertexBuffer.buffer};
    std::array<VkDeviceSize, 1> offsets{0};
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

    UniformBufferObject ubo{};
    ubo.model.rotate(time * 90.0F, 0.0F, 0.0F, 1.0F);
    ubo.view.lookAt(QVector3D{2.0F, 2.0F, 2.0F},
                    QVector3D{},
                    QVector3D{0.0F, 0.0F, 1.0F});
    auto swapChainImageSize = m_window->swapChainImageSize();
    auto ratio = static_cast<float>(swapChainImageSize.width()) / static_cast<float>(swapChainImageSize.height());
    ubo.proj.perspective(45.0F, ratio, 0.1F, 10.0F);
    ubo.proj(1, 1) = -ubo.proj(1, 1);

    VkDeviceMemory uniformBufferMemory = m_uniformBuffers.at(m_window->currentSwapChainImageIndex()).memory;
    void *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, uniformBufferMemory, 0, sizeOfUniformBufferObject, {}, &data),
                  "failed to map uniform buffer object memory");
    auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, uniformBufferMemory); });
    ubo.copyDataTo(reinterpret_cast<float *>(data));
}

void VulkanRenderer::createDescriptorPool()
{
    qDebug() << "Create descriptor pool";
    auto swapChainImageCount = m_window->swapChainImageCount();

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = swapChainImageCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
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
            bufferInfo.range = sizeOfUniformBufferObject;

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = *iDescriptorSets;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pBufferInfo = &bufferInfo;
            descriptorWrite.pImageInfo = nullptr;
            descriptorWrite.pTexelBufferView = nullptr;

            m_devFuncs->vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
        }
    }
}

void VulkanRenderer::createTextureImage()
{
    qDebug() << "Create texture image";
    QImage texture = QImage{textureName}
            .convertToFormat(QImage::Format::Format_RGB32);
    if (texture.isNull()) {
        throw std::runtime_error("failed to load texture image");
    }
    VkDeviceSize imageSize = texture.sizeInBytes();
    auto texWidth = texture.width();
    auto texHeight = texture.height();

    BufferWithMemory stagingBuffer{};
    createBuffer(imageSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex(), stagingBuffer);
    auto bufferGuard = sg::make_scope_guard([&, this]{ destroyBufferWithMemory(stagingBuffer); });

    void *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBuffer.memory, 0, imageSize, {}, &data),
                  "failed to map texture staging buffer memory");
    {
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, stagingBuffer.memory); });
        std::copy_n(static_cast<const uint8_t *>(texture.constBits()), imageSize, static_cast<uint8_t *>(data));
    }

    texture = {};

    auto format = VkFormat::VK_FORMAT_R8G8B8A8_SRGB;

    createImage(texWidth, texHeight, format, VkImageTiling::VK_IMAGE_TILING_OPTIMAL,
                VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_DST_BIT | VkImageUsageFlagBits::VK_IMAGE_USAGE_SAMPLED_BIT,
                m_window->deviceLocalMemoryIndex(), m_textureImage);
    transitionImageLayout(m_textureImage.image, format, VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    copyBufferToImage(stagingBuffer.buffer, m_textureImage.image, texWidth, texHeight);
    transitionImageLayout(m_textureImage.image, format, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanRenderer::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, uint32_t memoryTypeIndex, ImageWithMemory &image) const
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
    checkVkResult(m_devFuncs->vkCreateImage(m_device, &imageInfo, nullptr, &image.image),
                  "failed to create image");

    VkMemoryRequirements memRequirements{};
    m_devFuncs->vkGetImageMemoryRequirements(m_device, image.image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    checkVkResult(m_devFuncs->vkAllocateMemory(m_device, &allocInfo, nullptr, &image.memory),
                  "failed to allocate image memory");

    checkVkResult(m_devFuncs->vkBindImageMemory(m_device, image.image, image.memory, 0),
                  "failed to bind image memory");
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
    barrier.subresourceRange.aspectMask = VkImageAspectFlagBits::VK_IMAGE_ASPECT_COLOR_BIT;
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
    auto bufferGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkFreeCommandBuffers(m_device, commandPool, 1, &commandBuffer); });

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VkCommandBufferUsageFlagBits::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    checkVkResult(m_devFuncs->vkBeginCommandBuffer(commandBuffer, &beginInfo),
                  "failed to begin command buffer for copy");

    bufferGuard.dismiss();
    return commandBuffer;
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
