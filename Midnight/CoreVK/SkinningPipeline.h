#pragma once
/* Vulkan graphics pipeline with shaders */
#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"

namespace aveng {
    class SkinningPipeline {
    public:
        static bool init(EngineDevice& engineDevice, VkRenderPass& renderpass, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline, std::string vertexShaderFilename, std::string fragmentShaderFilename);
        static void cleanup(EngineDevice& engineDevice, VkPipeline& pipeline);
    };
}