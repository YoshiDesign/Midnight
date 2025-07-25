#pragma once

#include "EngineDevice.h"

#include <string>
#include <vector>

namespace aveng {

    /**
     * Configuration for compute pipelines
     */
    struct ComputePipelineConfig {
        ComputePipelineConfig() = default;
        ComputePipelineConfig(const ComputePipelineConfig&) = delete;
        ComputePipelineConfig& operator=(const ComputePipelineConfig&) = delete;
        
        VkPipelineLayout pipelineLayout = nullptr;
        
        // Specialization constants support
        VkSpecializationInfo* computeSpecializationInfo = nullptr;
    };

    /**
     * Vulkan Compute Pipeline implementation
     * Separate from graphics pipelines - used for general-purpose computing
     */
    class ComputePipeline {
        EngineDevice& engDevice;
        VkPipeline computePipeline;
        VkShaderModule compShaderModule;

    public:
        ComputePipeline(
            EngineDevice& device,
            const std::string& compFilepath,
            const ComputePipelineConfig& config
        );

        ~ComputePipeline();
        ComputePipeline(const ComputePipeline&) = delete;
        ComputePipeline& operator=(const ComputePipeline&) = delete;

        void bind(VkCommandBuffer commandBuffer);
        void dispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);

        static void defaultComputePipelineConfig(ComputePipelineConfig& configInfo);

    private:
        static std::vector<char> readFile(const std::string& filepath);
        void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);
        void createComputePipeline(const std::string& compFilepath, const ComputePipelineConfig& config);
    };

} // namespace aveng 