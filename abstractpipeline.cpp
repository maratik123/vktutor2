#include "abstractpipeline.h"

AbstractPipeline::AbstractPipeline(VulkanRenderer *vulkanRenderer)
    : m_vulkanRenderer{vulkanRenderer}
{
}

AbstractPipeline::~AbstractPipeline() = default;
