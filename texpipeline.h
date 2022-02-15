#ifndef TEXPIPELINE_H
#define TEXPIPELINE_H

#include "abstractpipeline.h"
#include "texvertex.h"

#include <QVector>

class TexVertex;

class TexPipeline final : public AbstractPipeline
{
public:
    explicit TexPipeline(VulkanRenderer *vulkanRenderer);

    void preInitResources() override;
    void initResources() override;
    void initSwapChainResources() override;
    [[nodiscard]] DescriptorPoolSizes descriptorPoolSizes(int swapChainImageCount) const override;
    void updateUniformBuffers(float time, const QSize &swapChainImageSize, int currentSwapChainImageIndex) const override;
    void drawCommands(VkCommandBuffer commandBuffer, int currentSwapChainImageIndex) const override;
    void releaseSwapChainResources() override;
    void releaseResources() override;

private:
    QVector<TexVertex> m_vertices;
    QVector<uint32_t> m_indices;
    BufferWithMemory m_vertexBuffer;
    BufferWithMemory m_indexBuffer;
    PipelineWithLayout m_graphicsPipelineWithLayout;
    QVector<VkDescriptorSet> m_descriptorSets;
    ShaderModules m_shaderModules;
    VkDescriptorSetLayout m_descriptorSetLayout;
    QVector<BufferWithMemory> m_vertUniformBuffers;
    QVector<BufferWithMemory> m_fragUniformBuffers;
    ImageWithMemory m_textureImage;
    VkImageView m_textureImageView;
    VkSampler m_textureSampler;
    uint32_t m_mipLevels;

    void loadModel();
    [[nodiscard]] PipelineWithLayout createGraphicsPipeline() const;
    [[nodiscard]] VkDescriptorSetLayout createDescriptorSetLayout() const;
    void createDescriptorSets(QVector<VkDescriptorSet> &descriptorSets) const;
    void createVertUniformBuffers();
    void createFragUniformBuffers();
    void createTextureImage();
    void createTextureSampler();
    void createTextureImageView();
};

#endif // TEXPIPELINE_H
