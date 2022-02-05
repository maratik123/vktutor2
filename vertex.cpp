#include "vertex.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <QHashFunctions>

VkVertexInputBindingDescription Vertex::createBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX;

    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::createAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, color);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VkFormat::VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    return attributeDescriptions;
}

uint qHash(const Vertex &key, uint seed) noexcept
{
    std::array hashes{
        std::hash<decltype(key.pos)>{}(key.pos),
        std::hash<decltype(key.color)>{}(key.color),
        std::hash<decltype(key.texCoord)>{}(key.texCoord)
    };
    return qHashRange(hashes.cbegin(), hashes.cend(), seed);
}

bool Vertex::operator==(const Vertex &other) const
{
    return pos == other.pos && color == other.color && texCoord == other.texCoord;
}
