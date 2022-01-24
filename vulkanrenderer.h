#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <QVulkanWindowRenderer>

class QFile;

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
    VkDevice m_device;
    QVulkanDeviceFunctions *m_devFuncs;
    bool m_msaa;

    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_graphicsPipeline;

    [[nodiscard]] VkShaderModule createShaderModule(const QByteArray &code);
    void createGraphicsPipeline();
};

#endif // VULKANRENDERER_H
