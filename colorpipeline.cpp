#include "colorpipeline.h"

#include "colorvertex.h"
#include "externals/scope_guard/scope_guard.hpp"

#include <QVulkanDeviceFunctions>

namespace {
const glm::vec3 lightCubeColor{1.0F, 1.0F, 0.0F};

const std::array<ColorVertex, 8> lightCubeVertices{
    ColorVertex{{-0.5F, -0.5F, 0.5F}, lightCubeColor},
    ColorVertex{{0.5F, -0.5F, 0.5F}, lightCubeColor},
    ColorVertex{{0.5F, 0.5F, 0.5F}, lightCubeColor},
    ColorVertex{{-0.5F, 0.5F, 0.5F}, lightCubeColor},
    ColorVertex{{-0.5F, -0.5F, -0.5F}, lightCubeColor},
    ColorVertex{{0.5F, -0.5F, -0.5F}, lightCubeColor},
    ColorVertex{{0.5F, 0.5F, -0.5F}, lightCubeColor},
    ColorVertex{{-0.5F, 0.5F, -0.5F}, lightCubeColor}
};
constexpr std::array<uint16_t, 36> lightCubeIndices{
    0, 1, 2, 2, 3, 0,
    6, 5, 4, 4, 7, 6,
    4, 0, 3, 3, 7, 4,
    2, 1, 5, 5, 6, 2,
    7, 3, 2, 2, 6, 7,
    1, 0, 4, 4, 5, 1
};

const QString colorVertShaderName = QStringLiteral(":/shaders/color.vert.spv");
const QString colorFragShaderName = QStringLiteral(":/shaders/color.frag.spv");

struct VertBindingObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 projView;
};
}

ColorPipeline::ColorPipeline(VulkanRenderer *vulkanRenderer)
    : AbstractPipeline{vulkanRenderer}
    , m_vertexBuffer{}
    , m_indexBuffer{}
    , m_graphicsPipelineWithLayout{}
    , m_shaderModules{}
    , m_descriptorSetLayout{}
{

}

void ColorPipeline::preInitResources()
{

}

void ColorPipeline::initResources()
{
    m_vertexBuffer = vulkanRenderer()->createVertexBuffer(lightCubeVertices);
    m_indexBuffer = vulkanRenderer()->createIndexBuffer(lightCubeIndices);
    m_shaderModules = vulkanRenderer()->createShaderModules(colorVertShaderName, colorFragShaderName);
    m_descriptorSetLayout = createDescriptorSetLayout();
}

void ColorPipeline::initSwapChainResources()
{
    createVertUniformBuffers();
    createDescriptorSets(m_descriptorSets);
    m_graphicsPipelineWithLayout = createGraphicsPipeline();
}

DescriptorPoolSizes ColorPipeline::descriptorPoolSizes(int swapChainImageCount) const
{
    return {
        {
            std::make_pair(VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, swapChainImageCount)
        },
        static_cast<uint32_t>(swapChainImageCount)
    };
}

void ColorPipeline::updateUniformBuffers(float time, const QSize &swapChainImageSize, int currentSwapChainImageIndex) const
{
    auto *devFuncs = vulkanRenderer()->devFuncs();
    VkDevice device = vulkanRenderer()->device();
    VmaAllocator allocator = vulkanRenderer()->allocator();
    VmaAllocation vertUniformBufferAllocation = m_vertUniformBuffers.at(currentSwapChainImageIndex).allocation;

    VertBindingObject *vertUbo{};

    VulkanRenderer::checkVkResult(vmaMapMemory(allocator, vertUniformBufferAllocation, reinterpret_cast<void **>(&vertUbo)),
                                  "failed to map uniform buffer object memory");
    auto mapGuard = sg::make_scope_guard([&]{ vmaUnmapMemory(allocator, vertUniformBufferAllocation); });

    vertUbo->model = glm::scale(glm::translate(glm::rotate(glm::mat4{1.0F}, -time * glm::radians(30.0F), glm::vec3{0.0F, 0.0F, 1.0F}), glm::vec3{-0.7F, 0.7F, 1.0F}), glm::vec3{0.05F, 0.05F, 0.05F});

    auto aspect = static_cast<float>(swapChainImageSize.width()) / static_cast<float>(swapChainImageSize.height());
    vertUbo->projView = glm::perspective(glm::radians(45.0F), aspect, 0.1F, 10.0F);
    vertUbo->projView[1][1] = -vertUbo->projView[1][1];

    glm::mat4 view = glm::lookAt(glm::vec3{2.0F, 2.0F, 2.0F}, glm::vec3{0.0F, 0.0F, 0.25F}, glm::vec3{0.0F, 0.0F, 1.0F});

    vertUbo->projView *= view;
}

void ColorPipeline::drawCommands(VkCommandBuffer commandBuffer, int currentSwapChainImageIndex) const
{
    auto *devFuncs = vulkanRenderer()->devFuncs();
    devFuncs->vkCmdBindPipeline(commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipelineWithLayout.pipeline);

    std::array vertexBuffers{m_vertexBuffer.buffer};
    std::array offsets{static_cast<VkDeviceSize>(0)};
    devFuncs->vkCmdBindVertexBuffers(commandBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets.data());

    devFuncs->vkCmdBindDescriptorSets(commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipelineWithLayout.layout, 0,
                                        1, &m_descriptorSets.at(currentSwapChainImageIndex), 0, nullptr);
    devFuncs->vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT16);

    devFuncs->vkCmdDrawIndexed(commandBuffer, lightCubeIndices.size(), 1, 0, 0, 0);

}

void ColorPipeline::releaseSwapChainResources()
{
    vulkanRenderer()->destroyPipelineWithLayout(m_graphicsPipelineWithLayout);
    vulkanRenderer()->destroyUniformBuffers(m_vertUniformBuffers);
}

void ColorPipeline::releaseResources()
{
    auto *devFuncs = vulkanRenderer()->devFuncs();
    VkDevice device = vulkanRenderer()->device();
    VmaAllocator allocator = vulkanRenderer()->allocator();
    devFuncs->vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    m_descriptorSetLayout = {};
    m_indexBuffer.destroy(allocator);
    m_vertexBuffer.destroy(allocator);
    vulkanRenderer()->destroyShaderModules(m_shaderModules);
}

VkDescriptorSetLayout ColorPipeline::createDescriptorSetLayout() const
{
    qDebug() << "Create descriptor set layout";

    std::array<VkDescriptorSetLayoutBinding, 1> bindings{};

    VkDescriptorSetLayoutBinding &uboLayoutBinding = bindings[0];
    uboLayoutBinding.binding = 0;
    uboLayoutBinding.descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
    uboLayoutBinding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();
    VkDescriptorSetLayout descriptorSetLayout{};
    VulkanRenderer::checkVkResult(vulkanRenderer()->devFuncs()->vkCreateDescriptorSetLayout(vulkanRenderer()->device(), &layoutInfo, nullptr, &descriptorSetLayout),
                                  "failed to create descriptor set layout");
    return descriptorSetLayout;
}

void ColorPipeline::createVertUniformBuffers()
{
    qDebug() << "Create vertex uniform buffers";

    vulkanRenderer()->createUniformBuffers<VertBindingObject>(m_vertUniformBuffers);
}

void ColorPipeline::createDescriptorSets(QVector<VkDescriptorSet> &descriptorSets) const
{
    qDebug() << "Create descriptors sets";
    auto *devFuncs = vulkanRenderer()->devFuncs();
    VkDevice device = vulkanRenderer()->device();
    auto swapChainImageCount = vulkanRenderer()->window()->swapChainImageCount();
    QVector<VkDescriptorSetLayout> layouts{swapChainImageCount, m_descriptorSetLayout};
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = vulkanRenderer()->descriptorPool();
    allocInfo.descriptorSetCount = swapChainImageCount;
    allocInfo.pSetLayouts = layouts.data();
    descriptorSets.clear();
    descriptorSets.resize(swapChainImageCount);
    VulkanRenderer::checkVkResult(devFuncs->vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()),
                                  "failed to allocate descriptor sets");

    const auto *iVertUniformBuffers = m_vertUniformBuffers.cbegin();
    const auto *iDescriptorSets = descriptorSets.cbegin();
    for (; iVertUniformBuffers != m_vertUniformBuffers.cend()
         && iDescriptorSets != descriptorSets.cend();
         ++iVertUniformBuffers, ++iDescriptorSets) {
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = iVertUniformBuffers->buffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(VertBindingObject);

        std::array<VkWriteDescriptorSet, 1> descriptorWrites{};

        descriptorWrites[0].sType = VkStructureType::VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = *iDescriptorSets;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferInfo;

        devFuncs->vkUpdateDescriptorSets(device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }

}

PipelineWithLayout ColorPipeline::createGraphicsPipeline() const
{
    qDebug() << "Create graphics pipeline";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

    VkPipelineShaderStageCreateInfo &vertShaderStageInfo = shaderStages[0];
    vertShaderStageInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = m_shaderModules.vert;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo &fragShaderStageInfo = shaderStages[1];
    fragShaderStageInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VkShaderStageFlagBits::VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = m_shaderModules.frag;
    fragShaderStageInfo.pName = "main";

    auto bindingDescription = ColorVertex::createBindingDescription();
    auto attributeDescriptions = ColorVertex::createAttributeDescriptions();

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

    auto *window = vulkanRenderer()->window();
    auto swapChainImageSize = window->swapChainImageSize();

    VkViewport viewport{};
    viewport.x = 0.0F;
    viewport.y = 0.0F;
    viewport.width = static_cast<float>(swapChainImageSize.width());
    viewport.height = static_cast<float>(swapChainImageSize.height());
    viewport.minDepth = 0.0F;
    viewport.maxDepth = 1.0F;

    VkRect2D scissor = VulkanRenderer::createVkRect2D(swapChainImageSize);

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

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples = window->sampleCountFlagBits();
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

    auto *devFuncs = vulkanRenderer()->devFuncs();
    VkDevice device = vulkanRenderer()->device();
    PipelineWithLayout pipelineWithLayout{};
    VulkanRenderer::checkVkResult(devFuncs->vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineWithLayout.layout),
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
    pipelineInfo.layout = pipelineWithLayout.layout;
    pipelineInfo.renderPass = window->defaultRenderPass();
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    VulkanRenderer::checkVkResult(devFuncs->vkCreateGraphicsPipelines(device, vulkanRenderer()->pipelineCache(), 1, &pipelineInfo, nullptr, &pipelineWithLayout.pipeline),
                                  "failed to create graphics pipeline");
    return pipelineWithLayout;
}
