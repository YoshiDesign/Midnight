#include <vector>
#include "ComputePipeline.h"
#include "CoreVK/Shader.h"
#include <cstdio>

namespace aveng {
    bool ComputePipeline::init(EngineDevice& engineDevice, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline, std::string computeShaderFilename) {
        /* shader */
        VkShaderModule computeModule = Shader::loadShader(engineDevice.device(), computeShaderFilename);

        if (computeModule == VK_NULL_HANDLE) {
            std::printf("%s error: could not load compute shader\n", __FUNCTION__);
            Shader::cleanup(engineDevice.device(), computeModule);
            return false;
        }
        VkPipelineShaderStageCreateInfo computeStageInfo{};
        computeStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computeStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computeStageInfo.module = computeModule;
        computeStageInfo.pName = "main";

        VkComputePipelineCreateInfo pipelineCreateInfo{};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.stage = computeStageInfo;

        VkResult result = vkCreateComputePipelines(engineDevice.device(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not create compute pipeline (error: %i)\n", __FUNCTION__, result);
            Shader::cleanup(engineDevice.device(), computeModule);
            return false;
        }

        /* it is safe to destroy the shader modules after pipeline has been created */
        Shader::cleanup(engineDevice.device(), computeModule);

        return true;
    }


    void ComputePipeline::cleanup(EngineDevice& engineDevice, VkPipeline& pipeline) {
        vkDestroyPipeline(engineDevice.device(), pipeline, nullptr);
    }
}