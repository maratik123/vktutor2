#include "vulkanrenderer.h"

#include "utils.h"
#include "settings.h"
#include "texpipeline.h"
#include "colorpipeline.h"

#include "externals/scope_guard/scope_guard.hpp"

#include <algorithm>
#include <chrono>
#include <string>

#include <QDebug>
#include <QVulkanDeviceFunctions>
#include <QLibrary>

namespace {
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

[[noreturn]] void unsupportedLayoutTransition(VkImageLayout oldLayout, VkImageLayout newLayout)
{
    std::string message{"unsupported layout transition from: "};
    message += std::to_string(oldLayout);
    message += " to: ";
    message += std::to_string(newLayout);
    throw std::runtime_error(message);
}

[[noreturn]] void throwErrorMessage(VkResult actualResult, const char *errorMessage, VkResult expectedResult)
{
    std::string message{errorMessage};
    message += ", expected result: ";
    message += std::to_string(expectedResult);
    message += ", actual result: ";
    message += std::to_string(actualResult);
    throw std::runtime_error{message};
}

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
    , m_allocator{}
    , m_pipelineCache{}
    , m_texShaderModules{}
    , m_colorShaderModules{}
    , m_descriptorPool{}
    , m_pipelines{std::make_unique<TexPipeline>(this), std::make_unique<ColorPipeline>(this)}
{
    qDebug() << "Create vulkan renderer";
}

void VulkanRenderer::preInitResources()
{
    qDebug() << "preInitResources";
    qDebug() << "Vulkan version: " << m_vkInst->apiVersion();
    m_window->setPreferredColorFormats({
            VkFormat::VK_FORMAT_B8G8R8A8_SRGB,
            VkFormat::VK_FORMAT_B8G8R8A8_UNORM
    });
    {
        auto supportedSampleCounts = m_window->supportedSampleCounts();
        m_window->setSampleCount(supportedSampleCounts.constLast());
    }
    for (const auto &pipeline : m_pipelines) {
        pipeline->preInitResources();
    }
}

void VulkanRenderer::initResources()
{
    qDebug() << "initResources";
    m_physDevice = m_window->physicalDevice();
    m_device = m_window->device();
    m_devFuncs = m_vkInst->deviceFunctions(m_device);
    m_allocator = createAllocator();
    m_pipelineCache = createPipelineCache();
    for (const auto &pipeline : m_pipelines) {
        pipeline->initResources();
    }
}

void VulkanRenderer::initSwapChainResources()
{
    qDebug() << "initSwapChainResources";
    updateDepthResources();
    m_descriptorPool = createDescriptorPool();
    for (const auto &pipeline : m_pipelines) {
        pipeline->initSwapChainResources();
    }
}

void VulkanRenderer::releaseSwapChainResources()
{
    qDebug() << "releaseSwapChainResources";
    for (const auto &pipeline : m_pipelines) {
        pipeline->releaseSwapChainResources();
    }
    m_devFuncs->vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    m_descriptorPool = {};
}

void VulkanRenderer::releaseResources()
{
    qDebug() << "releaseResources";
    for (const auto &pipeline : m_pipelines) {
        pipeline->releaseResources();
    }
    destroyShaderModules(m_texShaderModules);
    destroyShaderModules(m_colorShaderModules);
    savePipelineCache();
    m_devFuncs->vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
    m_pipelineCache = {};
    vmaDestroyAllocator(m_allocator);
    m_allocator = {};
    m_devFuncs = {};
    m_device = {};
    m_physDevice = {};
}

ShaderModules VulkanRenderer::createShaderModules(const QString &vertShaderName, const QString &fragShaderName) const
{
    ShaderModules shaderModules{};
    {
        auto vertShaderCode = readFile(vertShaderName);
        shaderModules.vert = createShaderModule(vertShaderCode);
    }
    try {
        auto fragShaderCode = readFile(fragShaderName);
        shaderModules.frag = createShaderModule(fragShaderCode);
        try {
            return shaderModules;
        } catch(...) {
            m_devFuncs->vkDestroyShaderModule(m_device, shaderModules.frag, nullptr);
            throw;
        }
    } catch(...) {
        m_devFuncs->vkDestroyShaderModule(m_device, shaderModules.vert, nullptr);
        throw;
    }
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

void VulkanRenderer::createUniformBuffers(QVector<BufferWithAllocation> &buffers, std::size_t size) const
{
    qDebug() << "Create uniform buffers";

    auto swapChainImageCount = m_window->swapChainImageCount();
    buffers.clear();
    buffers.reserve(swapChainImageCount);
    for (int i = 0; i < swapChainImageCount; ++i) {
        vmaSetCurrentFrameIndex(m_allocator, i);
        buffers << createBuffer(size, VkBufferUsageFlagBits::VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VmaMemoryUsage::VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
}

void VulkanRenderer::startNextFrame()
{
    auto currentSwapChainImageIndex = m_window->currentSwapChainImageIndex();
    vmaSetCurrentFrameIndex(m_allocator, currentSwapChainImageIndex);
    updateUniformBuffers(currentSwapChainImageIndex);
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_window->defaultRenderPass();
    renderPassInfo.framebuffer = m_window->currentFramebuffer();
    renderPassInfo.renderArea = createVkRect2D(m_window->swapChainImageSize());
    auto clearValues = createClearValues();
    renderPassInfo.clearValueCount = m_window->sampleCountFlagBits() > VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    renderPassInfo.pClearValues = clearValues.data();
    VkCommandBuffer commandBuffer = m_window->currentCommandBuffer();
    m_devFuncs->vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VkSubpassContents::VK_SUBPASS_CONTENTS_INLINE);
    for (const auto &pipeline : m_pipelines) {
        pipeline->drawCommands(commandBuffer, currentSwapChainImageIndex);
    }
    m_devFuncs->vkCmdEndRenderPass(commandBuffer);
    m_window->frameReady();
    m_window->requestUpdate();
}

void VulkanRenderer::updateUniformBuffers(int currentSwapChainImageIndex) const
{
    static auto startTime = std::chrono::high_resolution_clock::now();

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    auto swapChainImageSize = m_window->swapChainImageSize();

    for (const auto &pipeline : m_pipelines) {
        pipeline->updateUniformBuffers(time, swapChainImageSize, currentSwapChainImageIndex);
    }
}

VkDescriptorPool VulkanRenderer::createDescriptorPool() const
{
    qDebug() << "Create descriptor pool";
    auto swapChainImageCount = m_window->swapChainImageCount();

    uint32_t maxSets{};
    QVector<VkDescriptorPoolSize> poolSizes{};
    {
        QHash<VkDescriptorType, uint32_t> poolSizesDict{};
        for (const auto &pipeline : m_pipelines) {
            auto poolSizes = pipeline->descriptorPoolSizes(swapChainImageCount);
            maxSets += poolSizes.maxSets;
            poolSizesDict.reserve(poolSizes.poolSize.size());
            for (auto iPoolSize = poolSizes.poolSize.cbegin(); iPoolSize != poolSizes.poolSize.cend(); ++iPoolSize) {
                poolSizesDict[iPoolSize.key()] += iPoolSize.value();
            }
        }
        poolSizes.reserve(poolSizesDict.size());
        for (auto iPoolSize = poolSizesDict.cbegin(); iPoolSize != poolSizesDict.cend(); ++iPoolSize) {
            VkDescriptorPoolSize entry{};
            entry.type = iPoolSize.key();
            entry.descriptorCount = iPoolSize.value();
            poolSizes << entry;
            qDebug() << "Descriptor entry: (" << iPoolSize.key() << ", " << iPoolSize.value() << ")";
        }
    }

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSets;
    VkDescriptorPool descriptorPool{};
    checkVkResult(m_devFuncs->vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &descriptorPool),
                  "failed to create descriptor pool");
    return descriptorPool;
}

void VulkanRenderer::generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const
{
    qDebug() << "Generate mipmaps for levels: " << mipLevels;
    VkFormatProperties formatProperties{};
    m_funcs->vkGetPhysicalDeviceFormatProperties(m_physDevice, imageFormat, &formatProperties);
    VkFormatFeatureFlags expectedFeatures = VkFormatFeatureFlagBits::VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((formatProperties.optimalTilingFeatures & expectedFeatures) != expectedFeatures) {
        throw std::runtime_error{"texture image format does not support linear blitting"};
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
        if (mipWidth > 1) {
            mipWidth /= 2;
        }
        if (mipHeight > 1) {
            mipHeight /= 2;
        }
        blit.dstOffsets[1] = {mipWidth, mipHeight, 1};
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
    }

    qDebug() << "Generating level: " << mipLevels << ", mipWidth: " << mipWidth << ", mipHeight: " << mipHeight;

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


BufferWithAllocation VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const
{
    qDebug() << "Create buffer";

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VkSharingMode::VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;

    BufferWithAllocation result{};
    checkVkResult(vmaCreateBuffer(m_allocator, &bufferInfo, &allocationInfo, &result.buffer, &result.allocation, nullptr),
                  "failed to create buffer with allocation");
    return result;
}

ImageWithAllocation VulkanRenderer::createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
                                                VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage) const
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

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;

    ImageWithAllocation result{};
    checkVkResult(vmaCreateImage(m_allocator, &imageInfo, &allocationInfo, &result.image, &result.allocation, nullptr),
                  "failed to create image");
    return result;
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

    switch (oldLayout) {
    case VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED:
        barrier.srcAccessMask = {};

        sourceStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        switch (newLayout) {
        case VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;

            destinationStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT;
            break;
        case VkImageLayout::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VkAccessFlagBits::VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            destinationStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
            break;
        default:
            unsupportedLayoutTransition(oldLayout, newLayout);
        }
        break;
    case VkImageLayout::VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        if (newLayout == VkImageLayout::VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VkAccessFlagBits::VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VkAccessFlagBits::VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        } else {
            unsupportedLayoutTransition(oldLayout, newLayout);
        }
        break;
    default:
        unsupportedLayoutTransition(oldLayout, newLayout);
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

void VulkanRenderer::updateDepthResources() const
{
    qDebug() << "Update depth resources";
    transitionImageLayout(m_window->depthStencilImage(), m_window->depthStencilFormat(),
                          VkImageLayout::VK_IMAGE_LAYOUT_UNDEFINED, VkImageLayout::VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
}

VkPipelineCache VulkanRenderer::createPipelineCache() const
{
    qDebug() << "Create pipeline cache";
    auto pipelineCacheData = Settings::loadPipelineCache();
    VkPipelineCacheCreateInfo pipelineCacheInfo{};
    pipelineCacheInfo.sType = VkStructureType::VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    pipelineCacheInfo.initialDataSize = pipelineCacheData.size();
    pipelineCacheInfo.pInitialData = pipelineCacheData.constData();
    VkPipelineCache pipelineCache{};
    checkVkResult(m_devFuncs->vkCreatePipelineCache(m_device, &pipelineCacheInfo, nullptr, &pipelineCache),
                  "failed to create pipeline cache");
    return pipelineCache;
}

void VulkanRenderer::savePipelineCache() const
{
    qDebug() << "Save pipeline cache";
    std::size_t dataSize{};
    checkVkResult(m_devFuncs->vkGetPipelineCacheData(m_device, m_pipelineCache, &dataSize, nullptr),
                  "failed to get pipeline cache data size");
    QByteArray pipelineCacheData{static_cast<int>(dataSize), char{}};
    checkVkResult(m_devFuncs->vkGetPipelineCacheData(m_device, m_pipelineCache, &dataSize, pipelineCacheData.data()),
                  "failed to get pipeline cache data");
    pipelineCacheData.resize(static_cast<int>(dataSize));
    Settings::savePipelineCache(pipelineCacheData);
}

void VulkanRenderer::destroyUniformBuffers(QVector<BufferWithAllocation> &buffers) const
{
    qDebug() << "Destroy buffers";
    int i = 0;
    for (auto &buffer : buffers) {
        vmaSetCurrentFrameIndex(m_allocator, i);
        ++i;
        buffer.destroy(m_allocator);
    }
    buffers.clear();
}

void VulkanRenderer::destroyPipelineWithLayout(PipelineWithLayout &pipelineWithLayout) const
{
    qDebug() << "Destroy pipeline with layout";
    m_devFuncs->vkDestroyPipeline(m_device, pipelineWithLayout.pipeline, nullptr);
    m_devFuncs->vkDestroyPipelineLayout(m_device, pipelineWithLayout.layout, nullptr);
    pipelineWithLayout = {};
}

void VulkanRenderer::destroyShaderModules(ShaderModules &shaderModules) const
{
    qDebug() << "Destroy shader modules";
    m_devFuncs->vkDestroyShaderModule(m_device, shaderModules.frag, nullptr);
    m_devFuncs->vkDestroyShaderModule(m_device, shaderModules.vert, nullptr);
    shaderModules = {};
}

void VulkanRenderer::checkVkResult(VkResult actualResult, const char *errorMessage, VkResult expectedResult)
{
    if (Q_LIKELY(actualResult == expectedResult)) {
        return;
    }
    throwErrorMessage(actualResult, errorMessage, expectedResult);
}

VkRect2D VulkanRenderer::createVkRect2D(const QSize &rect)
{
    return {
        VkOffset2D{0, 0},
        VkExtent2D{
            static_cast<uint32_t>(rect.width()),
            static_cast<uint32_t>(rect.height())
        }
    };
}

VmaAllocator VulkanRenderer::createAllocator() const
{
    VmaVulkanFunctions vulkanFunctions{};
    //No valid method to get this in Qt5
    vulkanFunctions.vkGetInstanceProcAddr = {};

    vulkanFunctions.vkGetDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(m_vkInst->getInstanceProcAddr("vkGetDeviceProcAddr"));
    vulkanFunctions.vkGetPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(m_vkInst->getInstanceProcAddr("vkGetPhysicalDeviceProperties"));
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(m_vkInst->getInstanceProcAddr("vkGetPhysicalDeviceMemoryProperties2"));
    vulkanFunctions.vkGetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(m_vkInst->getInstanceProcAddr("vkGetPhysicalDeviceMemoryProperties"));

    vulkanFunctions.vkAllocateMemory = reinterpret_cast<PFN_vkAllocateMemory>(m_funcs->vkGetDeviceProcAddr(m_device, "vkAllocateMemory"));
    vulkanFunctions.vkFreeMemory = reinterpret_cast<PFN_vkFreeMemory>(m_funcs->vkGetDeviceProcAddr(m_device, "vkFreeMemory"));
    vulkanFunctions.vkMapMemory = reinterpret_cast<PFN_vkMapMemory>(m_funcs->vkGetDeviceProcAddr(m_device, "vkMapMemory"));
    vulkanFunctions.vkUnmapMemory = reinterpret_cast<PFN_vkUnmapMemory>(m_funcs->vkGetDeviceProcAddr(m_device, "vkUnmapMemory"));
    vulkanFunctions.vkFlushMappedMemoryRanges = reinterpret_cast<PFN_vkFlushMappedMemoryRanges>(m_funcs->vkGetDeviceProcAddr(m_device, "vkFlushMappedMemoryRanges"));
    vulkanFunctions.vkInvalidateMappedMemoryRanges = reinterpret_cast<PFN_vkInvalidateMappedMemoryRanges>(m_funcs->vkGetDeviceProcAddr(m_device, "vkInvalidateMappedMemoryRanges"));
    vulkanFunctions.vkBindBufferMemory = reinterpret_cast<PFN_vkBindBufferMemory>(m_funcs->vkGetDeviceProcAddr(m_device, "vkBindBufferMemory"));
    vulkanFunctions.vkBindImageMemory = reinterpret_cast<PFN_vkBindImageMemory>(m_funcs->vkGetDeviceProcAddr(m_device, "vkBindImageMemory"));
    vulkanFunctions.vkGetBufferMemoryRequirements = reinterpret_cast<PFN_vkGetBufferMemoryRequirements>(m_funcs->vkGetDeviceProcAddr(m_device, "vkGetBufferMemoryRequirements"));
    vulkanFunctions.vkGetImageMemoryRequirements = reinterpret_cast<PFN_vkGetImageMemoryRequirements>(m_funcs->vkGetDeviceProcAddr(m_device, "vkGetImageMemoryRequirements"));
    vulkanFunctions.vkCreateBuffer = reinterpret_cast<PFN_vkCreateBuffer>(m_funcs->vkGetDeviceProcAddr(m_device, "vkCreateBuffer"));
    vulkanFunctions.vkDestroyBuffer = reinterpret_cast<PFN_vkDestroyBuffer>(m_funcs->vkGetDeviceProcAddr(m_device, "vkDestroyBuffer"));
    vulkanFunctions.vkCreateImage = reinterpret_cast<PFN_vkCreateImage>(m_funcs->vkGetDeviceProcAddr(m_device, "vkCreateImage"));
    vulkanFunctions.vkDestroyImage = reinterpret_cast<PFN_vkDestroyImage>(m_funcs->vkGetDeviceProcAddr(m_device, "vkDestroyImage"));
    vulkanFunctions.vkCmdCopyBuffer = reinterpret_cast<PFN_vkCmdCopyBuffer>(m_funcs->vkGetDeviceProcAddr(m_device, "vkCmdCopyBuffer"));
    vulkanFunctions.vkGetBufferMemoryRequirements2KHR = reinterpret_cast<PFN_vkGetBufferMemoryRequirements2>(m_funcs->vkGetDeviceProcAddr(m_device, "vkGetBufferMemoryRequirements2"));
    vulkanFunctions.vkGetImageMemoryRequirements2KHR = reinterpret_cast<PFN_vkGetImageMemoryRequirements2>(m_funcs->vkGetDeviceProcAddr(m_device, "vkGetImageMemoryRequirements2"));
    vulkanFunctions.vkBindBufferMemory2KHR = reinterpret_cast<PFN_vkBindBufferMemory2>(m_funcs->vkGetDeviceProcAddr(m_device, "vkBindBufferMemory2"));
    vulkanFunctions.vkBindImageMemory2KHR = reinterpret_cast<PFN_vkBindImageMemory2>(m_funcs->vkGetDeviceProcAddr(m_device, "vkBindImageMemory2"));

    VmaAllocatorCreateInfo allocatorInfo{};
    auto vulkanVersion = m_vkInst->apiVersion();
    // NOLINTNEXTLINE(hicpp-signed-bitwise)
    allocatorInfo.vulkanApiVersion = VK_MAKE_API_VERSION(0, vulkanVersion.majorVersion(), vulkanVersion.minorVersion(), 0U);
    allocatorInfo.physicalDevice = m_physDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_vkInst->vkInstance();
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;

    VmaAllocator allocator{};
    checkVkResult(vmaCreateAllocator(&allocatorInfo, &allocator),
                  "Can not create allocator");
    return allocator;
}
