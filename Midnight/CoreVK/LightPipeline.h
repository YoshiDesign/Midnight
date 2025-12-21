#pragma once
/* Vulkan graphics pipeline with shaders */

#include <string>
#include <vulkan/vulkan.h>
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"

namespace aveng {
    class LightPipeline {
    public:
        static bool init(EngineDevice& engineDevice, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline,
            VkRenderPass renderpass, uint32_t numColorAttachments, bool colorMask, std::string vertexShaderFilename, std::string fragmentShaderFilename);
        static void cleanup(EngineDevice& engineDevice, VkPipeline& pipeline);
    };
}