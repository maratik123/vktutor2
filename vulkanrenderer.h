#ifndef VULKANRENDERER_H
#define VULKANRENDERER_H

#include <QVulkanWindowRenderer>

struct QueueFamilyIndices;

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
    QVulkanFunctions *const m_funcs;
    QVulkanDeviceFunctions *m_devFuncs;

    void pickPhysicalDevice();
    void createLogicalDevice();
    [[nodiscard]] bool isDeviceSuitable(const VkPhysicalDeviceProperties &) const;
    [[nodiscard]] QueueFamilyIndices findQueueFamilies() const;
};

#endif // VULKANRENDERER_H
