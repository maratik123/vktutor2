#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <QVulkanWindowRenderer>

class ModelVertex;

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
    uint32_t m_mipLevels;
    ImageWithMemory m_textureImage;
    VkImageView m_textureImageView;
    VkSampler m_textureSampler;

    QVector<BufferWithMemory> m_vertUniformBuffers;
    QVector<BufferWithMemory> m_fragUniformBuffers;
    VkDescriptorPool m_descriptorPool;
    QVector<VkDescriptorSet> m_descriptorSets;

    QVector<ModelVertex> m_vertices;
    QVector<uint32_t> m_indices;

    [[nodiscard]] VkShaderModule createShaderModule(const QByteArray &code) const;
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createDescriptorPool();
    void createDescriptorSets();

    void loadModel();

    void createVertexBuffer();
    void createIndexBuffer();

    void createVertUniformBuffers();
    void createFragUniformBuffers();
    template<typename T>
    void createUniformBuffers(QVector<BufferWithMemory> &buffers) const;
    void createUniformBuffers(QVector<BufferWithMemory> &buffers, std::size_t size) const;

    void createTextureImage();
    void createTextureImageView();
    void createTextureSampler();

    void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) const;

    void createDepthResources() const;

    [[nodiscard]] BufferWithMemory createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryTypeIndex) const;
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) const;

    [[nodiscard]] ImageWithMemory createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples,
                                              VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, uint32_t memoryTypeIndex) const;
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height) const;
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) const;

    [[nodiscard]] VkImageView createImageView(VkImage image, VkFormat format, uint32_t mipLevels) const;

    [[nodiscard]] VkCommandBuffer beginSingleTimeCommands() const;
    void endSingleTimeCommands(VkCommandBuffer commandBuffer) const;


    void updateUniformBuffer() const;

    void destroyBufferWithMemory(BufferWithMemory &buffer) const;
    void destroyImageWithMemory(ImageWithMemory &image) const;
    void destroyUniformBuffers(QVector<BufferWithMemory> &buffers) const;
};

#endif // VULKANRENDERER_H
