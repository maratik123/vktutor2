#ifndef VERTEX_H
#define VERTEX_H

#include "glm.h"

#include <array>

#include <QVulkanInstance>

struct ModelVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    [[nodiscard]] bool operator==(const ModelVertex &other) const;

    [[nodiscard]] static VkVertexInputBindingDescription createBindingDescription();
    [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 3> createAttributeDescriptions();
};

uint qHash(const ModelVertex &key, uint seed = 0) noexcept;

struct ColorVertex
{
    glm::vec3 pos;
    glm::vec3 color;

    [[nodiscard]] static VkVertexInputBindingDescription createBindingDescription();
    [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 2> createAttributeDescriptions();
};

#endif // VERTEX_H
