#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <QVulkanWindowRenderer>

#include "abstractpipeline.h"

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
    [[nodiscard]] BufferWithMemory createVertexBuffer(const QVector<T> &vertices) const;
    template<typename T>
    [[nodiscard]] BufferWithMemory createIndexBuffer(const QVector<T> &indices) const;
    static void checkVkResult(VkResult actualResult, const char *errorMessage, VkResult expectedResult = VkResult::VK_SUCCESS);
    void destroyBufferWithMemory(BufferWithMemory &buffer) const;
    [[nodiscard]] QVulkanDeviceFunctions *devFuncs() const { return m_devFuncs; }
    [[nodiscard]] VkDevice device() const { return m_device; }
    [[nodiscard]] static VkRect2D createVkRect2D(const QSize &rect);
    [[nodiscard]] ShaderModules createShaderModules(const QString &vertShaderName, const QString &fragShaderName) const;
    void destroyShaderModules(ShaderModules &shaderModules) const;
    [[nodiscard]] QVulkanWindow *window() const { return m_window; }
    [[nodiscard]] VkPipelineCache pipelineCache() const { return m_pipelineCache; }
    void destroyPipelineWithLayout(PipelineWithLayout &pipelineWithLayout) const;
    [[nodiscard]] VkDescriptorPool descriptorPool() const { return m_descriptorPool; }
    template<typename T>
    void createUniformBuffers(QVector<BufferWithMemory> &buffers) const { createUniformBuffers(buffers, sizeof(T)); }
    void destroyUniformBuffers(QVector<BufferWithMemory> &buffers) const;
    void destroyImageWithMemory(ImageWithMemory &image) const;
    [[nodiscard]] BufferWithMemory createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryTypeIndex) const;
    [[nodiscard]] ImageWithMemory createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
                                              VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, uint32_t memoryTypeIndex) const;
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) const;
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
    void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const;
    [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format, uint32_t mipLevels) const;

private:
    std::array<std::unique_ptr<AbstractPipeline>, 2> m_pipelines;
    QVulkanWindow *const m_window;
    QVulkanInstance *const m_vkInst;
    QVulkanFunctions *const m_funcs;
    VkPhysicalDevice m_physDevice;
    VkDevice m_device;
    QVulkanDeviceFunctions *m_devFuncs;

    VkPipelineCache m_pipelineCache;

    ShaderModules m_texShaderModules;
    ShaderModules m_colorShaderModules;

    VkDescriptorPool m_descriptorPool;

    [[nodiscard]] VkShaderModule createShaderModule(const QByteArray &code) const;

    void savePipelineCache() const;
    void createPipelineCache();

    [[nodiscard]] VkDescriptorPool createDescriptorPool() const;

    template<typename T>
    [[nodiscard]] BufferWithMemory createBuffer(const QVector<T> &vec, VkBufferUsageFlags usage) const;

    void createUniformBuffers(QVector<BufferWithMemory> &buffers, std::size_t size) const;

    void createDepthResources() const;

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const;

    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;

    void updateUniformBuffers() const;
};

#endif // VULKANRENDERER_H
