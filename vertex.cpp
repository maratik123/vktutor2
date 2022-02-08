#include "vertex.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <QHashFunctions>

VkVertexInputBindingDescription ModelVertex::createBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(ModelVertex);
    bindingDescription.inputRate = VkVertexInputRate::VK_VERTEX_INPUT_RATE_VERTEX;

    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> ModelVertex::createAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(ModelVertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VkFormat::VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(ModelVertex, normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VkFormat::VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(ModelVertex, texCoord);

    return attributeDescriptions;
}

uint qHash(const ModelVertex &key, uint seed) noexcept
{
    std::array hashes{
        std::hash<decltype(key.pos)>{}(key.pos),
        std::hash<decltype(key.texCoord)>{}(key.texCoord),
        std::hash<decltype(key.normal)>{}(key.normal)
    };
    return qHashRange(hashes.cbegin(), hashes.cend(), seed);
}

bool ModelVertex::operator==(const ModelVertex &other) const
{
    return pos == other.pos && texCoord == other.texCoord && normal == other.normal;
}
