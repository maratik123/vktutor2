#ifndef OBJECTWITHALLOCATION_H
#define OBJECTWITHALLOCATION_H

#include "vkmemalloc.h"

template<typename T>
struct ObjectWithAllocation
{
    T object;
    VmaAllocation allocation;

    void destroy(VmaAllocator allocator);
};

struct BufferWithAllocation : ObjectWithAllocation<VkBuffer>
{
    VkDeviceSize size;
    VkBufferUsageFlags usage;
};

#endif // OBJECTWITHALLOCATION_H
