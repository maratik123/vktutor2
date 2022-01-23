#include "vulkanrenderer.h"

#include "queuefamilyindices.h"
#include "utils.h"

#include <bitset>

#include <QDebug>
#include <QVulkanFunctions>

namespace {
const QVector<VkFormat> colorFormat = {
    VK_FORMAT_B8G8R8A8_SRGB,
    VK_FORMAT_B8G8R8A8_UNORM
};
}

VulkanRenderer::VulkanRenderer(QVulkanWindow *w)
    : m_window{w}
    , m_vkInst{m_window->vulkanInstance()}
    , m_funcs{m_vkInst->functions()}
{
}

void VulkanRenderer::startNextFrame()
{
    qDebug() << "startNextFrame";
}

void VulkanRenderer::preInitResources()
{
    qDebug() << "preInitResources";
    m_window->setPreferredColorFormats(colorFormat);
    QVulkanWindowRenderer::preInitResources();
}

void VulkanRenderer::initResources()
{
    qDebug() << "initResources";
    QVulkanWindowRenderer::initResources();
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
