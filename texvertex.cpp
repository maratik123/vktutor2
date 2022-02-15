#include "texvertex.h"

#include <numeric>
#include <functional>

#include <QHashFunctions>

VkVertexInputBindingDescription TexVertex::createBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(TexVertex);
    bindingDescription.inputRate = VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX;

    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> TexVertex::createAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(TexVertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(TexVertex, normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VkFormat::VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(TexVertex, texCoord);

    return attributeDescriptions;
}

bool TexVertex::operator==(const TexVertex &other) const
{
    return pos == other.pos && normal == other.normal && texCoord == other.texCoord;
}

uint qHash(const TexVertex &key, uint seed) noexcept
{
    std::hash<float> hasher{};
    std::array data{
        key.pos.x,
        key.pos.y,
        key.pos.x,
        key.normal.x,
        key.normal.y,
        key.normal.z,
        key.texCoord.x,
        key.texCoord.y
    };
    // combiner taken from N3876 / boost::hash_combine
    return qHash(std::accumulate(data.cbegin(), data.cend(), std::hash<uint>{}(seed), [hasher](auto s, auto t){ return s ^ (hasher(t) + 0x9e3779b9UL + (s << 6U) + (s >> 2U)); }), seed);
}
