#include "colorpipeline.h"

#include "colorvertex.h"

namespace {
const std::array<ColorVertex, 8> lightCubeVertices{
    ColorVertex{{-0.5F, -0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}},
    ColorVertex{{0.5F, -0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}},
    ColorVertex{{0.5F, 0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}},
    ColorVertex{{-0.5F, 0.5F, 0.5F}, {1.0F, 1.0F, 1.0F}},
    ColorVertex{{-0.5F, -0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}},
    ColorVertex{{0.5F, -0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}},
    ColorVertex{{0.5F, 0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}},
    ColorVertex{{-0.5F, 0.5F, -0.5F}, {1.0F, 1.0F, 1.0F}}
};
constexpr std::array<uint16_t, 36> lightCubeIndices{
    0, 1, 2, 2, 3, 0,
    6, 5, 4, 4, 7, 6,
    4, 0, 3, 3, 7, 4,
    2, 1, 5, 5, 6, 2,
    7, 3, 2, 2, 6, 7,
    1, 0, 4, 4, 5, 1
};

const QString colorVertShaderName = QStringLiteral(":/shaders/color.vert.spv");
const QString colorFragShaderName = QStringLiteral(":/shaders/color.frag.spv");
}

ColorPipeline::ColorPipeline(VulkanRenderer *vulkanRenderer)
    : AbstractPipeline{vulkanRenderer}
{

}

void ColorPipeline::preInitResources()
{

}

void ColorPipeline::initResources()
{

}

void ColorPipeline::initSwapChainResources()
{

}

void ColorPipeline::updateUniformBuffers(float time, const QSize &swapChainImageSize, int currentSwapChainImageIndex) const
{

}

void ColorPipeline::drawCommands(VkCommandBuffer commandBuffer, int currentSwapChainImageIndex) const
{

}

void ColorPipeline::releaseSwapChainResources()
{

}

void ColorPipeline::releaseResources()
{

}
