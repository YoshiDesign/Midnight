#pragma once

#include <string>
#include <vulkan/vulkan_core.h>
#include "CoreVK/EngineDevice.h"

namespace aveng {
    class TerrainPipeline {
    public:
        static bool init(EngineDevice& engineDevice, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline,
            VkRenderPass renderpass, uint32_t numColorAttachments, std::string vertexShaderFilename, std::string fragmentShaderFilename);
        static void cleanup(EngineDevice& engineDevice, VkPipeline& pipeline);
    };
}