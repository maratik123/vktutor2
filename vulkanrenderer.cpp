#include "vulkanrenderer.h"

#include "externals/scope_guard/scope_guard.hpp"
#include "utils.h"

#include <algorithm>
#include <array>
#include <sstream>

#include <QDebug>
#include <QFile>
#include <QVector2D>
#include <QVector3D>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>

namespace {
const QString vertShaderName = QStringLiteral(":/shaders/shader.vert.spv");
const QString fragShaderName = QStringLiteral(":/shaders/shader.frag.spv");

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

[[nodiscard]] constexpr std::array<VkClearValue, 3> createClearValues()
{
    constexpr VkClearColorValue clearColor{{0.0F, 0.0F, 0.0F, 1.0F}};
    constexpr VkClearDepthStencilValue clearDepthStencil{1.0F, 0};
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

constexpr std::array<Vertex, 3> vertices{
    Vertex{QVector2D{0.0F, -0.5F}, QVector3D{1.0F, 0.0F, 0.0F}},
    Vertex{QVector2D{0.5F, 0.5F}, QVector3D{0.0F, 1.0F, 0.0F}},
    Vertex{QVector2D{-0.5F, 0.5F}, QVector3D{0.0F, 0.0F, 1.0F}}
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
}

VulkanRenderer::VulkanRenderer(QVulkanWindow *w)
    : m_window{w}
    , m_vkInst{m_window->vulkanInstance()}
    , m_funcs{m_vkInst->functions()}
    , m_physDevice{}
    , m_device{}
    , m_devFuncs{}
    , m_msaa{}
    , m_pipelineLayout{}
    , m_graphicsPipeline{}
    , m_vertexBuffer{}
    , m_vertexBufferMemory{}
{
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
    QVulkanWindowRenderer::initResources();
}

void VulkanRenderer::initSwapChainResources()
{
    qDebug() << "initSwapChainResources";
    createGraphicsPipeline();
    createVertexBuffer();
    QVulkanWindowRenderer::initSwapChainResources();
}

void VulkanRenderer::releaseSwapChainResources()
{
    qDebug() << "releaseSwapChainResources";
    m_devFuncs->vkDestroyBuffer(m_device, m_vertexBuffer, nullptr);
    m_devFuncs->vkFreeMemory(m_device, m_vertexBufferMemory, nullptr);
    m_devFuncs->vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
    m_devFuncs->vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    QVulkanWindowRenderer::releaseSwapChainResources();
}

void VulkanRenderer::releaseResources()
{
    qDebug() << "releaseResources";
    m_physDevice = {};
    m_devFuncs = {};
    m_device = {};
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

VkShaderModule VulkanRenderer::createShaderModule(const QByteArray &code)
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
    rasterizer.frontFace = VkFrontFace::VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0F;
    rasterizer.depthBiasClamp = 0.0F;
    rasterizer.depthBiasSlopeFactor = 0.0F;

    auto sampleCountFlagBits = m_window->sampleCountFlagBits();

    m_msaa = sampleCountFlagBits > VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;

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

//    std::array dynamicStates{
//        VkDynamicState::VK_DYNAMIC_STATE_VIEWPORT,
//        VkDynamicState::VK_DYNAMIC_STATE_LINE_WIDTH
//    };

//    VkPipelineDynamicStateCreateInfo dynamicState{};
//    dynamicState.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
//    dynamicState.dynamicStateCount = dynamicStates.size();
//    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
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

    VkBuffer stagingBuffer{};
    VkDeviceMemory stagingBufferMemory{};
    createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex(), stagingBuffer, stagingBufferMemory);

    void *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBufferMemory, 0, bufferSize, {}, &data),
                  "failed to map memory to staging buffer");
    std::copy(vertices.cbegin(), vertices.cend(), reinterpret_cast<Vertex *>(data));
    m_devFuncs->vkUnmapMemory(m_device, stagingBufferMemory);

    createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 m_window->deviceLocalMemoryIndex(), m_vertexBuffer, m_vertexBufferMemory);

    copyBuffer(stagingBuffer, m_vertexBuffer, bufferSize);

    m_devFuncs->vkDestroyBuffer(m_device, stagingBuffer, nullptr);
    m_devFuncs->vkFreeMemory(m_device, stagingBufferMemory, nullptr);
}

void VulkanRenderer::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandPool commandPool = m_window->graphicsCommandPool();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VkCommandBufferLevel::VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer{};
    checkVkResult(m_devFuncs->vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer),
                  "failed to allocate command buffer for copy");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VkCommandBufferUsageFlagBits::VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    checkVkResult(m_devFuncs->vkBeginCommandBuffer(commandBuffer, &beginInfo),
                  "failed to begin command buffer for copy");

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = size;
    m_devFuncs->vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);
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

    m_devFuncs->vkFreeCommandBuffers(m_device, commandPool, 1, &commandBuffer);
}

void VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryTypeIndex, VkBuffer &buffer, VkDeviceMemory &bufferMemory)
{
    qDebug() << "Create buffer";

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;

    checkVkResult(m_devFuncs->vkCreateBuffer(m_device, &bufferInfo, nullptr, &buffer),
                  "failed to create buffer");

    VkMemoryRequirements memRequirements{};
    m_devFuncs->vkGetBufferMemoryRequirements(m_device, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = memoryTypeIndex;
    checkVkResult(m_devFuncs->vkAllocateMemory(m_device, &allocInfo, nullptr, &bufferMemory),
                  "failed to allocate buffer memory");
    checkVkResult(m_devFuncs->vkBindBufferMemory(m_device, buffer, bufferMemory, 0),
                  "failed to bind vertex buffer to memory");
}

void VulkanRenderer::startNextFrame()
{
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_window->defaultRenderPass();
    renderPassInfo.framebuffer = m_window->currentFramebuffer();
    renderPassInfo.renderArea = createVkRect2D(m_window->swapChainImageSize());
    std::array<VkClearValue, 3> clearValues = createClearValues();
    renderPassInfo.clearValueCount = m_msaa ? 3 : 2;
    renderPassInfo.pClearValues = clearValues.data();
    VkCommandBuffer commandBuffer = m_window->currentCommandBuffer();
    m_devFuncs->vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VkSubpassContents::VK_SUBPASS_CONTENTS_INLINE);
    m_devFuncs->vkCmdBindPipeline(commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    std::array<VkBuffer, 1> vertexBuffers{m_vertexBuffer};
    std::array<VkDeviceSize, 1> offsets{0};
    m_devFuncs->vkCmdBindVertexBuffers(commandBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets.data());

    m_devFuncs->vkCmdDraw(commandBuffer, vertices.size(), 1, 0, 0);
    m_devFuncs->vkCmdEndRenderPass(commandBuffer);
    m_window->frameReady();
}
