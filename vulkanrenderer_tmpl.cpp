#include "vulkanrenderer.h"

#include "texvertex.h"
#include "colorvertex.h"

#include <QVulkanDeviceFunctions>

#include "externals/scope_guard/scope_guard.hpp"

template<typename T>
BufferWithMemory VulkanRenderer::createVertexBuffer(const QVector<T> &vertices) const
{
    qDebug() << "Create vertex buffer";

    return createBuffer<T>(vertices.cbegin(), vertices.cend(), VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

template<typename T>
BufferWithMemory VulkanRenderer::createIndexBuffer(const QVector<T> &indices) const
{
    qDebug() << "Create index buffer";

    return createBuffer<T>(indices.cbegin(), indices.cend(), VkBufferUsageFlagBits::VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

template<typename T, std::size_t Size>
BufferWithMemory VulkanRenderer::createVertexBuffer(const std::array<T, Size> &vertices) const
{
    qDebug() << "Create vertex buffer";

    return createBuffer<T>(vertices.cbegin(), vertices.cend(), VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

template<typename T, std::size_t Size>
BufferWithMemory VulkanRenderer::createIndexBuffer(const std::array<T, Size> &indices) const
{
    qDebug() << "Create index buffer";

    return createBuffer<T>(indices.cbegin(), indices.cend(), VkBufferUsageFlagBits::VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

template<typename T, typename Iterator>
BufferWithMemory VulkanRenderer::createBuffer(Iterator begin, Iterator end, VkBufferUsageFlags usage) const
{
    qDebug() << "Create buffer";

    VkDeviceSize bufferSize = std::distance(begin, end) * sizeof(T);

    auto stagingBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, m_window->hostVisibleMemoryIndex());
    auto bufferGuard = sg::make_scope_guard([&, this]{ destroyBufferWithMemory(stagingBuffer); });

    T *data{};
    checkVkResult(m_devFuncs->vkMapMemory(m_device, stagingBuffer.memory, 0, bufferSize, {}, reinterpret_cast<void **>(&data)),
                  "failed to map memory to staging buffer");
    {
        auto mapGuard = sg::make_scope_guard([&, this]{ m_devFuncs->vkUnmapMemory(m_device, stagingBuffer.memory); });
        std::copy(begin, end, data);
    }

    BufferWithMemory deviceBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                                                 m_window->deviceLocalMemoryIndex());

    copyBuffer(stagingBuffer.buffer, deviceBuffer.buffer, bufferSize);

    return deviceBuffer;
}

template BufferWithMemory VulkanRenderer::createVertexBuffer(const QVector<TexVertex> &vertices) const;
template BufferWithMemory VulkanRenderer::createIndexBuffer(const QVector<uint32_t> &indices) const;
template BufferWithMemory VulkanRenderer::createVertexBuffer(const std::array<ColorVertex, 8> &vertices) const;
template BufferWithMemory VulkanRenderer::createIndexBuffer(const std::array<uint16_t, 36> &indices) const;
