#ifndef ABSTRACTPIPELINE_H
#define ABSTRACTPIPELINE_H

class VulkanRenderer;

#include <QVulkanInstance>
#include <QHash>

struct DescriptorPoolSizes
{
    QHash<VkDescriptorType, uint32_t> poolSize;
    uint32_t maxSets;
};

class AbstractPipeline
{
public:
    explicit AbstractPipeline(VulkanRenderer *vulkanRenderer);

    AbstractPipeline(const AbstractPipeline &) = delete;
    AbstractPipeline(AbstractPipeline &&) = delete;
    AbstractPipeline &operator=(const AbstractPipeline &) = delete;
    AbstractPipeline &operator=(AbstractPipeline &&) = delete;

    virtual ~AbstractPipeline();

    virtual void preInitResources() = 0;
    virtual void initResources() = 0;
    virtual void initSwapChainResources() = 0;
    [[nodiscard]] virtual DescriptorPoolSizes descriptorPoolSizes(int swapChainImageCount) const = 0;
    virtual void updateUniformBuffers(float time, const QSize &swapChainImageSize, int currentSwapChainImageIndex) const = 0;
    virtual void drawCommands(VkCommandBuffer commandBuffer, int currentSwapChainImageIndex) const = 0;
    virtual void releaseSwapChainResources() = 0;
    virtual void releaseResources() = 0;

protected:
    [[nodiscard]] VulkanRenderer *vulkanRenderer() const { return m_vulkanRenderer; }

private:
    VulkanRenderer *m_vulkanRenderer;
};

#endif // ABSTRACTPIPELINE_H
