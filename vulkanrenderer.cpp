#include "vulkanrenderer.h"

#include "queuefamilyindices.h"
#include "utils.h"

#include <bitset>

#include <QDebug>
#include <QVulkanFunctions>

VulkanRenderer::VulkanRenderer(QVulkanWindow *w)
    : m_window{w}
    , m_funcs{w->vulkanInstance()->functions()}
    , m_devFuncs{nullptr}
{
}

void VulkanRenderer::startNextFrame()
{
    qDebug() << "startNextFrame";
}

void VulkanRenderer::pickPhysicalDevice()
{
    qDebug() << "Picking physical device";
    const auto &physicalDevices = m_window->availablePhysicalDevices();
    if (physicalDevices.empty()) {
        throw std::runtime_error("No physical devices available");
    }
    int suitableDeviceIndex = 0;
    for (int i = 0; i < physicalDevices.size(); ++i) {
        m_window->setPhysicalDeviceIndex(i);
        if (isDeviceSuitable(physicalDevices[i])) {
            break;
        }
    }
}

bool VulkanRenderer::isDeviceSuitable(const VkPhysicalDeviceProperties &physicalDeviceProperties) const
{
    qDebug() << "Finding suitable device";
    auto indices = findQueueFamilies();
    return indices.isComplete();
}

void VulkanRenderer::preInitResources()
{
    qDebug() << "preInitResources";
    pickPhysicalDevice();
    QVulkanWindowRenderer::preInitResources();
}

QueueFamilyIndices VulkanRenderer::findQueueFamilies() const
{
    qDebug() << "Finding queue families";
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    auto *physicalDevice = m_window->physicalDevice();
    m_funcs->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    QVector<VkQueueFamilyProperties> queueFamilies(static_cast<int>(queueFamilyCount));
    m_funcs->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    for(int i = 0; i < queueFamilies.size(); ++i) {
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }
    }

    return indices;
}

void VulkanRenderer::initResources()
{
    qDebug() << "initResources";
    createLogicalDevice();
    QVulkanWindowRenderer::initResources();
}

void VulkanRenderer::createLogicalDevice()
{
    qDebug() << "Creating logical device";
    auto queueFamilyIndices = findQueueFamilies();
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0F;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = 0;

    const auto &layers = m_window->vulkanInstance()->layers();
    createInfo.enabledLayerCount = layers.size();


    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.
    }
}

void VulkanRenderer::initSwapChainResources()
{
    qDebug() << "initSwapChainResources";
    QVulkanWindowRenderer::initSwapChainResources();
}

void VulkanRenderer::releaseSwapChainResources()
{
    qDebug() << "releaseSwapChainResources";
    QVulkanWindowRenderer::releaseSwapChainResources();
}

void VulkanRenderer::releaseResources()
{
    qDebug() << "releaseResources";
    QVulkanWindowRenderer::releaseResources();
}

void VulkanRenderer::physicalDeviceLost()
{
    qDebug() << "physicalDeviceLost";
    QVulkanWindowRenderer::physicalDeviceLost();
}

void VulkanRenderer::logicalDeviceLost()
{
    qDebug() << "logicalDeviceLost";
    QVulkanWindowRenderer::logicalDeviceLost();
}
