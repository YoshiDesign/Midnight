/* Vulkan compute pipeline with shaders */
#pragma once

#include <string>
#include <vulkan/vulkan.h>

#include "VkRenderData.h"
namespace aveng{

    class EngineDevice;

    class ComputePipeline {
    public:
        static bool init(EngineDevice& engineDevice, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline, std::string computeShaderFilename);
        static void cleanup(EngineDevice& engineDevice, VkPipeline& pipeline);
    };
}