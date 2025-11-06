/* Vulkan Pipeline Layout */
#pragma once
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"

namespace aveng {
    class PipelineLayout {
    public:
        static bool init(EngineDevice& engineDevice, VkPipelineLayout& pipelineLayout,
            std::vector<VkDescriptorSetLayout> layouts, std::vector<VkPushConstantRange> pushConstants = {});

        static void cleanup(EngineDevice& engineDevice, VkPipelineLayout& pipelineLayout);
    };
}