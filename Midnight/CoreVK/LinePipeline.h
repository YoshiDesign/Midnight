#pragma once

#include <string>
#include <vulkan/vulkan.h>

#include "CoreVK/VkRenderData.h"
#include "CoreVK/EngineDevice.h"

namespace aveng {
    
    class LinePipeline {
    public:
        static bool init(EngineDevice& engineDevice, VkRenderPass renderpass, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline,
             std::string vertexShaderFilename, std::string fragmentShaderFilename);
        static void cleanup(EngineDevice& engineDevice, VkPipeline& pipeline);
    };

}
