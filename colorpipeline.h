#ifndef COLORPIPELINE_H
#define COLORPIPELINE_H

#include "abstractpipeline.h"
#include "vulkanrenderer.h"

class ColorPipeline final : public AbstractPipeline
{
public:
    explicit ColorPipeline(VulkanRenderer *vulkanRenderer);

    void preInitResources() override;
    void initResources() override;
    void initSwapChainResources() override;
    [[nodiscard]] DescriptorPoolSizes descriptorPoolSizes(int swapChainImageCount) const override;
    void updateUniformBuffers(float time, const QSize &swapChainImageSize, int currentSwapChainImageIndex) const override;
    void drawCommands(VkCommandBuffer commandBuffer, int currentSwapChainImageIndex) const override;
    void releaseSwapChainResources() override;
    void releaseResources() override;

private:
    BufferWithAllocation m_vertexBuffer;
    BufferWithAllocation m_indexBuffer;
    PipelineWithLayout m_graphicsPipelineWithLayout;
    QVector<VkDescriptorSet> m_descriptorSets;
    ShaderModules m_shaderModules;
    VkDescriptorSetLayout m_descriptorSetLayout;
    QVector<BufferWithAllocation> m_vertUniformBuffers;

    [[nodiscard]] PipelineWithLayout createGraphicsPipeline() const;
    [[nodiscard]] VkDescriptorSetLayout createDescriptorSetLayout() const;
    void createDescriptorSets(QVector<VkDescriptorSet> &descriptorSets) const;
    void createVertUniformBuffers();
};

#endif // COLORPIPELINE_H
