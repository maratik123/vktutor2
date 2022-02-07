#ifndef VERTEX_H
#define VERTEX_H

#include "glm.h"

#include <array>

#include <QVulkanInstance>

struct Vertex
{
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec2 texCoord;
    glm::vec3 normal;

    [[nodiscard]] bool operator==(const Vertex &other) const;

    [[nodiscard]] static VkVertexInputBindingDescription createBindingDescription();
    [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 4> createAttributeDescriptions();
};

uint qHash(const Vertex &key, uint seed = 0) noexcept;

#endif // VERTEX_H
