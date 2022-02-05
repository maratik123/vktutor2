#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <QVulkanWindowRenderer>

class Vertex;

struct BufferWithMemory {
    VkBuffer buffer;
    VkDeviceMemory memory;
};

struct ImageWithMemory {
    VkImage image;
    VkDeviceMemory memory;
};

class VulkanRenderer : public QVulkanWindowRenderer
{
public:
    explicit VulkanRenderer(QVulkanWindow *w);

    void startNextFrame() override;    
    void preInitResources() override;
    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
    void physicalDeviceLost() override;
    void logicalDeviceLost() override;

private:
    QVulkanWindow *const m_window;
    QVulkanInstance *const m_vkInst;
    QVulkanFunctions *const m_funcs;
    VkPhysicalDevice m_physDevice;
    VkDevice m_device;
    QVulkanDeviceFunctions *m_devFuncs;
    bool m_msaa;

    VkDescriptorSetLayout m_descriptorSetLayout;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_graphicsPipeline;
    BufferWithMemory m_vertexBuffer;
    BufferWithMemory m_indexBuffer;
    ImageWithMemory m_textureImage;
    VkImageView m_textureImageView;
    VkSampler m_textureSampler;

    QVector<BufferWithMemory> m_uniformBuffers;
    VkDescriptorPool m_descriptorPool;
    QVector<VkDescriptorSet> m_descriptorSets;

    QVector<Vertex> m_vertices;
    QVector<uint32_t> m_indices;

    [[nodiscard]] VkShaderModule createShaderModule(const QByteArray &code) const;
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createVertexBuffer();
    void createIndexBuffer();
    void createUniformBuffers();
    [[nodiscard]] BufferWithMemory createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryTypeIndex) const;
    [[nodiscard]] ImageWithMemory createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, uint32_t memoryTypeIndex) const;
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const;
    void destroyBufferWithMemory(const BufferWithMemory &buffer) const;
    void destroyImageWithMemory(const ImageWithMemory &image) const;
    void updateUniformBuffer() const;
    void createDescriptorPool();
    void createDescriptorSets();
    void createTextureImage();
    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) const;
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
    void createTextureImageView();
    [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format) const;
    void createTextureSampler();
    void createDepthResources() const;
    void loadModel();
};

#endif // VULKANRENDERER_H
