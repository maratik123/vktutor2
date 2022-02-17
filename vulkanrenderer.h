#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <QVulkanWindowRenderer>
#include "vkmemalloc.h"

#include "abstractpipeline.h"
#include "objectwithallocation.h"

struct PipelineWithLayout
{
    VkPipelineLayout layout;
    VkPipeline pipeline;
};

struct ShaderModules
{
    VkShaderModule vert;
    VkShaderModule frag;
};

class VulkanRenderer final : public QVulkanWindowRenderer
{
public:
    explicit VulkanRenderer(QVulkanWindow *w);

    void startNextFrame() override;    
    void preInitResources() override;
    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;

    template<typename T>
    [[nodiscard]] BufferWithAllocation createVertexBuffer(const QVector<T> &vertices) const;
    template<typename T>
    [[nodiscard]] BufferWithAllocation createIndexBuffer(const QVector<T> &indices) const;
    template<typename T, std::size_t Size>
    [[nodiscard]] BufferWithAllocation createVertexBuffer(const std::array<T, Size> &vertices) const;
    template<typename T, std::size_t Size>
    [[nodiscard]] BufferWithAllocation createIndexBuffer(const std::array<T, Size> &indices) const;
    static void checkVkResult(VkResult actualResult, const char *errorMessage, VkResult expectedResult = VkResult::VK_SUCCESS);
    [[nodiscard]] static VkRect2D createVkRect2D(const QSize &rect);
    [[nodiscard]] ShaderModules createShaderModules(const QString &vertShaderName, const QString &fragShaderName) const;
    void destroyShaderModules(ShaderModules &shaderModules) const;
    void destroyPipelineWithLayout(PipelineWithLayout &pipelineWithLayout) const;
    template<typename T>
    void createUniformBuffers(QVector<BufferWithAllocation> &buffers) const { createUniformBuffers(buffers, sizeof(T)); }
    void destroyUniformBuffers(QVector<BufferWithAllocation> &buffers) const;
    [[nodiscard]] BufferWithAllocation createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage) const;
    [[nodiscard]] ObjectWithAllocation<VkImage> createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
                                                  VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VmaMemoryUsage memoryUsage) const;
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) const;
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
    void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const;
    [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format, uint32_t mipLevels) const;

    [[nodiscard]] VmaAllocator allocator() const { return m_allocator; }
    [[nodiscard]] QVulkanDeviceFunctions *devFuncs() const { return m_devFuncs; }
    [[nodiscard]] VkDevice device() const { return m_device; }
    [[nodiscard]] QVulkanWindow *window() const { return m_window; }
    [[nodiscard]] VkPipelineCache pipelineCache() const { return m_pipelineCache; }
    [[nodiscard]] VkDescriptorPool descriptorPool() const { return m_descriptorPool; }

private:
    std::array<std::unique_ptr<AbstractPipeline>, 2> m_pipelines;
    QVulkanWindow *const m_window;
    QVulkanInstance *const m_vkInst;
    QVulkanFunctions *const m_funcs;
    VkPhysicalDevice m_physDevice;
    VkDevice m_device;
    QVulkanDeviceFunctions *m_devFuncs;
    VmaAllocator m_allocator;

    VkPipelineCache m_pipelineCache;

    ShaderModules m_texShaderModules;
    ShaderModules m_colorShaderModules;

    VkDescriptorPool m_descriptorPool;

    [[nodiscard]] VkShaderModule createShaderModule(const QByteArray &code) const;

    void savePipelineCache() const;
    [[nodiscard]] VkPipelineCache createPipelineCache() const;

    [[nodiscard]] VkDescriptorPool createDescriptorPool() const;

    template<typename T, typename Iterator>
    [[nodiscard]] BufferWithAllocation createBuffer(Iterator begin, Iterator end, VkBufferUsageFlags usage) const;

    void createUniformBuffers(QVector<BufferWithAllocation> &buffers, std::size_t size) const;

    void updateDepthResources() const;

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const;

    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    void updateUniformBuffers(int currentSwapChainImageIndex) const;
    [[nodiscard]] VmaAllocator createAllocator() const;
};

#endif // VULKANRENDERER_H
