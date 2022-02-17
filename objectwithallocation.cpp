#include "objectwithallocation.h"

template<>
void ObjectWithAllocation<VkBuffer>::destroy(VmaAllocator allocator)
{
    vmaDestroyBuffer(allocator, object, allocation);
    *this = {};
}

template<>
void ObjectWithAllocation<VkImage>::destroy(VmaAllocator allocator)
{
    vmaDestroyImage(allocator, object, allocation);
    *this = {};
}
