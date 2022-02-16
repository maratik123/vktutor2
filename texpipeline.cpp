#include "texpipeline.h"

#include "externals/scope_guard/scope_guard.hpp"
#include "vulkanrenderer.h"
#include "utils.h"
#include "texvertex.h"
#include "model.h"

#include <QVulkanDeviceFunctions>
#include <QImage>
#include <QColorSpace>

namespace {
const QString texVertShaderName = QStringLiteral(":/shaders/tex.vert.spv");
const QString texFragShaderName = QStringLiteral(":/shaders/tex.frag.spv");
const QString modelDirName = QStringLiteral(":/models");
const QString modelName = QStringLiteral("viking_room.obj");
const QString textureName = QStringLiteral(":/textures/viking_room.png");

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
}

TexPipeline::TexPipeline(VulkanRenderer *vulkanRenderer)
    : AbstractPipeline{vulkanRenderer}
    , m_vertexBuffer{}
    , m_indexBuffer{}
    , m_graphicsPipelineWithLayout{}
    , m_shaderModules{}
    , m_descriptorSetLayout{}
    , m_textureImage{}
    , m_textureImageView{}
    , m_textureSampler{}
    , m_mipLevels{}
{
}

void TexPipeline::preInitResources()
{
    loadModel();
}

void TexPipeline::initResources()
{
    m_vertexBuffer = vulkanRenderer()->createVertexBuffer(m_vertices);
    m_indexBuffer = vulkanRenderer()->createIndexBuffer(m_indices);
    m_shaderModules = vulkanRenderer()->createShaderModules(texVertShaderName, texFragShaderName);
    m_descriptorSetLayout = createDescriptorSetLayout();
    createTextureImage();
    createTextureImageView();
    createTextureSampler();
}

void TexPipeline::initSwapChainResources()
{
    createVertUniformBuffers();
    createFragUniformBuffers();
    createDescriptorSets(m_descriptorSets);
    m_graphicsPipelineWithLayout = createGraphicsPipeline();
}

DescriptorPoolSizes TexPipeline::descriptorPoolSizes(int swapChainImageCount) const
{
    return {
        {
            std::make_pair(VkDescriptorType::VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * swapChainImageCount),
            std::make_pair(VkDescriptorType::VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, swapChainImageCount)
        },
        static_cast<uint32_t>(swapChainImageCount)
    };
}

void TexPipeline::updateUniformBuffers(float time, const QSize &swapChainImageSize, int currentSwapChainImageIndex) const
{
    auto *devFuncs = vulkanRenderer()->devFuncs();
    VkDevice device = vulkanRenderer()->device();
    {
        VkDeviceMemory vertUniformBufferMemory = m_vertUniformBuffers.at(currentSwapChainImageIndex).memory;

        VertBindingObject *vertUbo{};

        VulkanRenderer::checkVkResult(devFuncs->vkMapMemory(device, vertUniformBufferMemory, 0, sizeof(VertBindingObject), {}, reinterpret_cast<void **>(&vertUbo)),
                                      "failed to map uniform buffer object memory");
        auto mapGuard = sg::make_scope_guard([&]{ devFuncs->vkUnmapMemory(device, vertUniformBufferMemory); });

        vertUbo->model = glm::rotate(glm::mat4{1.0F}, time * glm::radians(6.0F), glm::vec3{0.0F, 0.0F, 1.0F});

        vertUbo->modelInvTrans = glm::transpose(glm::inverse(vertUbo->model));

        auto aspect = static_cast<float>(swapChainImageSize.width()) / static_cast<float>(swapChainImageSize.height());
        vertUbo->projView = glm::perspective(glm::radians(45.0F), aspect, 0.1F, 10.0F);
        vertUbo->projView[1][1] = -vertUbo->projView[1][1];

        glm::mat4 view = glm::lookAt(glm::vec3{2.0F, 2.0F, 2.0F}, glm::vec3{0.0F, 0.0F, 0.25F}, glm::vec3{0.0F, 0.0F, 1.0F});

        vertUbo->projView *= view;
    }
    {
        VkDeviceMemory fragUniformBufferMemory = m_fragUniformBuffers.at(currentSwapChainImageIndex).memory;

        FragBindingObject *fragUbo{};

        VulkanRenderer::checkVkResult(devFuncs->vkMapMemory(device, fragUniformBufferMemory, 0, sizeof(FragBindingObject), {}, reinterpret_cast<void **>(&fragUbo)),
                                      "failed to map light info memory");
        auto mapGuard = sg::make_scope_guard([&]{ devFuncs->vkUnmapMemory(device, fragUniformBufferMemory); });

        glm::mat4 modelDiffuseLightPos = glm::rotate(glm::mat4{1.0F}, -time * glm::radians(30.0F), glm::vec3{0.0F, 0.0F, 1.0F});

        fragUbo->ambientColor = {0.01F, 0.01F, 0.01F, 1.0F};
        fragUbo->diffuseLightPos = modelDiffuseLightPos * glm::vec4{-0.7F, 0.7F, 1.0F, 1.0F};
        fragUbo->diffuseLightColor = {1.0F, 1.0F, 0.0F, 1.0F};
    }
}

void TexPipeline::drawCommands(VkCommandBuffer commandBuffer, int currentSwapChainImageIndex) const
{
    auto *devFuncs = vulkanRenderer()->devFuncs();
    devFuncs->vkCmdBindPipeline(commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipelineWithLayout.pipeline);

    std::array vertexBuffers{m_vertexBuffer.buffer};
    std::array offsets{static_cast<VkDeviceSize>(0)};
    devFuncs->vkCmdBindVertexBuffers(commandBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets.data());

    devFuncs->vkCmdBindDescriptorSets(commandBuffer, VkPipelineBindPoint::VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipelineWithLayout.layout, 0,
                                        1, &m_descriptorSets.at(currentSwapChainImageIndex), 0, nullptr);
    devFuncs->vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);

    devFuncs->vkCmdDrawIndexed(commandBuffer, m_indices.size(), 1, 0, 0, 0);
}

void TexPipeline::releaseSwapChainResources()
{
    vulkanRenderer()->destroyPipelineWithLayout(m_graphicsPipelineWithLayout);
    vulkanRenderer()->destroyUniformBuffers(m_fragUniformBuffers);
    vulkanRenderer()->destroyUniformBuffers(m_vertUniformBuffers);
}

void TexPipeline::releaseResources()
{
    auto *devFuncs = vulkanRenderer()->devFuncs();
    VkDevice device = vulkanRenderer()->device();
    devFuncs->vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);
    m_descriptorSetLayout = {};
    vulkanRenderer()->destroyShaderModules(m_shaderModules);
    vulkanRenderer()->destroyBufferWithMemory(m_indexBuffer);
    vulkanRenderer()->destroyBufferWithMemory(m_vertexBuffer);
    devFuncs->vkDestroySampler(device, m_textureSampler, nullptr);
    m_textureSampler = {};
    devFuncs->vkDestroyImageView(device, m_textureImageView, nullptr);
    m_textureImageView = {};
    vulkanRenderer()->destroyImageWithMemory(m_textureImage);
    m_descriptorSetLayout = {};
}

PipelineWithLayout TexPipeline::createGraphicsPipeline() const
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

    auto bindingDescription = TexVertex::createBindingDescription();
    auto attributeDescriptions = TexVertex::createAttributeDescriptions();

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

    auto swapChainImageSize = vulkanRenderer()->window()->swapChainImageSize();

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
    multisampling.rasterizationSamples = vulkanRenderer()->window()->sampleCountFlagBits();
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

    PipelineWithLayout pipelineWithLayout{};
    VulkanRenderer::checkVkResult(vulkanRenderer()->devFuncs()->vkCreatePipelineLayout(vulkanRenderer()->device(), &pipelineLayoutInfo, nullptr, &pipelineWithLayout.layout),
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
    pipelineInfo.renderPass = vulkanRenderer()->window()->defaultRenderPass();
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    VulkanRenderer::checkVkResult(vulkanRenderer()->devFuncs()->vkCreateGraphicsPipelines(vulkanRenderer()->device(), vulkanRenderer()->pipelineCache(), 1, &pipelineInfo, nullptr, &pipelineWithLayout.pipeline),
                                  "failed to create graphics pipeline");
    return pipelineWithLayout;
}

VkDescriptorSetLayout TexPipeline::createDescriptorSetLayout() const
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
    VkDescriptorSetLayout descriptorSetLayout{};
    VulkanRenderer::checkVkResult(vulkanRenderer()->devFuncs()->vkCreateDescriptorSetLayout(vulkanRenderer()->device(), &layoutInfo, nullptr, &descriptorSetLayout),
                                  "failed to create descriptor set layout");
    return descriptorSetLayout;
}

void TexPipeline::createDescriptorSets(QVector<VkDescriptorSet> &descriptorSets) const
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
    const auto *iFragUniformBuffers = m_fragUniformBuffers.cbegin();
    const auto *iDescriptorSets = descriptorSets.cbegin();
    for (; iVertUniformBuffers != m_vertUniformBuffers.cend()
         && iFragUniformBuffers != m_fragUniformBuffers.cend()
         && iDescriptorSets != descriptorSets.cend();
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

        devFuncs->vkUpdateDescriptorSets(device, descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
    }
}

void TexPipeline::createVertUniformBuffers()
{
    qDebug() << "Create vertex uniform buffers";

    vulkanRenderer()->createUniformBuffers<VertBindingObject>(m_vertUniformBuffers);
}

void TexPipeline::createFragUniformBuffers()
{
    qDebug() << "Create fragment uniform buffers";

    vulkanRenderer()->createUniformBuffers<FragBindingObject>(m_fragUniformBuffers);
}

void TexPipeline::loadModel()
{
    qDebug() << "Load model";
    auto model = Model::loadModel(modelDirName, modelName);
    m_vertices.swap(model.vertices);
    m_indices.swap(model.indices);
}

void TexPipeline::createTextureImage()
{
    qDebug() << "Create texture image";
    BufferWithMemory stagingBuffer{};
    int texWidth{};
    int texHeight{};
    auto *window = vulkanRenderer()->window();
    auto *devFuncs = vulkanRenderer()->devFuncs();
    VkDevice device = vulkanRenderer()->device();
    try {
        QImage texture = QImage{textureName}
                .convertToFormat(QImage::Format::Format_RGBA8888);
        texture.convertToColorSpace(QColorSpace::NamedColorSpace::SRgb);
        if (texture.isNull()) {
            throw std::runtime_error{"failed to load texture image"};
        }
        VkDeviceSize imageSize = texture.sizeInBytes();
        texWidth = texture.width();
        texHeight = texture.height();
        m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

        stagingBuffer = vulkanRenderer()->createBuffer(imageSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, window->hostVisibleMemoryIndex());

        uint8_t *data{};
        VulkanRenderer::checkVkResult(devFuncs->vkMapMemory(device, stagingBuffer.memory, 0, imageSize, {}, reinterpret_cast<void **>(&data)),
                                      "failed to map texture staging buffer memory");
        auto mapGuard = sg::make_scope_guard([&]{ devFuncs->vkUnmapMemory(device, stagingBuffer.memory); });
        std::copy_n(texture.constBits(), imageSize, data);
    } catch (...) {
        vulkanRenderer()->destroyBufferWithMemory(stagingBuffer);
        throw;
    }

    auto bufferGuard = sg::make_scope_guard([&, this]{ vulkanRenderer()->destroyBufferWithMemory(stagingBuffer); });
    VkImageUsageFlags usage = static_cast<VkImageUsageFlags>(VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
            | VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_DST_BIT
            | VkImageUsageFlagBits::VK_IMAGE_USAGE_SAMPLED_BIT;

    m_textureImage = vulkanRenderer()->createImage(texWidth, texHeight, m_mipLevels, VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT,
                                                   textureFormat, VkImageTiling::VK_IMAGE_TILING_OPTIMAL, usage, window->deviceLocalMemoryIndex());
    vulkanRenderer()->transitionImageLayout(m_textureImage.image, textureFormat, VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED, VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_mipLevels);
    vulkanRenderer()->copyBufferToImage(stagingBuffer.buffer, m_textureImage.image, texWidth, texHeight);
    vulkanRenderer()->generateMipmaps(m_textureImage.image, textureFormat, texWidth, texHeight, m_mipLevels);
}

void TexPipeline::createTextureSampler()
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
    VulkanRenderer::checkVkResult(vulkanRenderer()->devFuncs()->vkCreateSampler(vulkanRenderer()->device(), &samplerInfo, nullptr, &m_textureSampler),
                                  "failed to create texture sampler");
}

void TexPipeline::createTextureImageView()
{
    qDebug() << "Create texture image view";
    m_textureImageView = vulkanRenderer()->createImageView(m_textureImage.image, textureFormat, m_mipLevels);
}
