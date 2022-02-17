#include "vulkanrenderer.h"

#include "texvertex.h"
#include "colorvertex.h"

#include <QVulkanDeviceFunctions>

#include "externals/scope_guard/scope_guard.hpp"

template<typename T>
BufferWithAllocation VulkanRenderer::createVertexBuffer(const QVector<T> &vertices) const
{
    qDebug() << "Create vertex buffer";

    return createBuffer<T>(vertices.cbegin(), vertices.cend(), VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

template<typename T>
BufferWithAllocation VulkanRenderer::createIndexBuffer(const QVector<T> &indices) const
{
    qDebug() << "Create index buffer";

    return createBuffer<T>(indices.cbegin(), indices.cend(), VkBufferUsageFlagBits::VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

template<typename T, std::size_t Size>
BufferWithAllocation VulkanRenderer::createVertexBuffer(const std::array<T, Size> &vertices) const
{
    qDebug() << "Create vertex buffer";

    return createBuffer<T>(vertices.cbegin(), vertices.cend(), VkBufferUsageFlagBits::VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
}

template<typename T, std::size_t Size>
BufferWithAllocation VulkanRenderer::createIndexBuffer(const std::array<T, Size> &indices) const
{
    qDebug() << "Create index buffer";

    return createBuffer<T>(indices.cbegin(), indices.cend(), VkBufferUsageFlagBits::VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
}

template<typename T, typename Iterator>
BufferWithAllocation VulkanRenderer::createBuffer(Iterator begin, Iterator end, VkBufferUsageFlags usage) const
{
    qDebug() << "Create buffer";

    VkDeviceSize bufferSize = std::distance(begin, end) * sizeof(T);

    auto stagingBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VmaMemoryUsage::VMA_MEMORY_USAGE_CPU_ONLY);
    auto bufferGuard = sg::make_scope_guard([&, this]{ stagingBuffer.destroy(m_allocator); });

    T *data{};
    checkVkResult(vmaMapMemory(m_allocator, stagingBuffer.allocation, reinterpret_cast<void **>(&data)),
                  "failed to map memory to staging buffer");
    {
        auto mapGuard = sg::make_scope_guard([&, this]{ vmaUnmapMemory(m_allocator, stagingBuffer.allocation); });
        std::copy(begin, end, data);
    }

    auto deviceBuffer = createBuffer(bufferSize, VkBufferUsageFlagBits::VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage,
                                                 VmaMemoryUsage::VMA_MEMORY_USAGE_GPU_ONLY);
    try {
        copyBuffer(stagingBuffer.buffer, deviceBuffer.buffer, bufferSize);

        return deviceBuffer;
    } catch(...) {
        vmaUnmapMemory(m_allocator, deviceBuffer.allocation);
        throw;
    }
}

template BufferWithAllocation VulkanRenderer::createVertexBuffer(const QVector<TexVertex> &vertices) const;
template BufferWithAllocation VulkanRenderer::createIndexBuffer(const QVector<uint32_t> &indices) const;
template BufferWithAllocation VulkanRenderer::createVertexBuffer(const std::array<ColorVertex, 8> &vertices) const;
template BufferWithAllocation VulkanRenderer::createIndexBuffer(const std::array<uint16_t, 36> &indices) const;
