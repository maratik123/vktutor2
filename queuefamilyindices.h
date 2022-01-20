#ifndef QUEUEFAMILYINDICES_H
#define QUEUEFAMILYINDICES_H

#include <optional>
#include <cstdint>

struct QueueFamilyIndices
{
    std::optional<std::uint32_t> graphicsFamily;

    [[nodiscard]] bool isComplete() const {
        return graphicsFamily.has_value();
    }
};

#endif // QUEUEFAMILYINDICES_H
