#ifndef COLORVERTEX_H
#define COLORVERTEX_H

#include "glm.h"

#include <array>

#include <QVulkanInstance>

struct ColorVertex
{
    glm::vec3 pos;
    glm::vec3 color;

    [[nodiscard]] static VkVertexInputBindingDescription createBindingDescription();
    [[nodiscard]] static std::array<VkVertexInputAttributeDescription, 2> createAttributeDescriptions();
};

#endif // COLORVERTEX_H
