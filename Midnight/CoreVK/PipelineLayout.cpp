#include "PipelineLayout.h"


namespace aveng {
    bool PipelineLayout::init(EngineDevice& engineDevice, VkPipelineLayout& pipelineLayout,
        std::vector<VkDescriptorSetLayout> layouts, std::vector<VkPushConstantRange> pushConstants) {

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        pipelineLayoutInfo.pSetLayouts = layouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstants.size());
        pipelineLayoutInfo.pPushConstantRanges = pushConstants.data();

        VkResult result = vkCreatePipelineLayout(engineDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not create pipeline layout (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        return true;
    }

    void PipelineLayout::cleanup(EngineDevice& engineDevice, VkPipelineLayout& pipelineLayout) {
        vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
    }
}