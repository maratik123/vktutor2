#ifndef TEXVERTEX_H
#define TEXVERTEX_H

#include "glm.h"

#include <array>

#include <QVulkanInstance>

struct TexVertex
{
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    [[nodiscard]] bool operator==(const TexVertex &other) const;

    [[nodiscard]] static VkVertexInputBindingDescription createBindingDescription();
    [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 3> createAttributeDescriptions();
};

uint qHash(const TexVertex &key, uint seed = 0) noexcept;

#endif // TEXVERTEX_H
