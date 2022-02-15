#ifndef COLORPIPELINE_H
#define COLORPIPELINE_H

#include "abstractpipeline.h"

class ColorPipeline final : public AbstractPipeline
{
public:
    explicit ColorPipeline(VulkanRenderer *vulkanRenderer);

    void preInitResources() override;
    void initResources() override;
    void initSwapChainResources() override;
    void updateUniformBuffers(float time, const QSize &swapChainImageSize, int currentSwapChainImageIndex) const override;
    void drawCommands(VkCommandBuffer commandBuffer, int currentSwapChainImageIndex) const override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
};

#endif // COLORPIPELINE_H
