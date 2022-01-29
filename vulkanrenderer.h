#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <QVulkanWindowRenderer>

class QFile;

struct BufferWithMemory {
    VkBuffer buffer;
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

    QVector<BufferWithMemory> m_uniformBuffers;
    VkDescriptorPool m_descriptorPool;
    QVector<VkDescriptorSet> m_descriptorSets;

    [[nodiscard]] VkShaderModule createShaderModule(const QByteArray &code);
    void createDescriptorSetLayout();
    void createGraphicsPipeline();
    void createVertexBuffer();
    void createIndexBuffer();
    void createUniformBuffers();
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, uint32_t memoryTypeIndex, BufferWithMemory &buffer);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    void destroyBufferWithMemory(const BufferWithMemory &buffer);
    void updateUniformBuffer();
    void createDescriptorPool();
    void createDescriptorSets();
};

#endif // VULKANRENDERER_H
