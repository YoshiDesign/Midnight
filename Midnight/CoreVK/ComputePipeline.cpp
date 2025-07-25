#include "ComputePipeline.h"

#include <cassert>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace aveng {

    ComputePipeline::ComputePipeline(
        EngineDevice& device,
        const std::string& compFilepath,
        const ComputePipelineConfig& config
    ) : engDevice{ device }
    {
        createComputePipeline(compFilepath, config);
    }

    ComputePipeline::~ComputePipeline()
    {
        vkDestroyShaderModule(engDevice.device(), compShaderModule, nullptr);
        vkDestroyPipeline(engDevice.device(), computePipeline, nullptr);
    }

    void ComputePipeline::createComputePipeline(
        const std::string& compFilepath,
        const ComputePipelineConfig& configInfo
    )
    {
        assert(
            configInfo.pipelineLayout != VK_NULL_HANDLE &&
            "Cannot create compute pipeline: no pipelineLayout provided in configInfo");

        auto compCode = readFile(compFilepath);

        std::cout << "Compute Shader: " << compCode.size() << " bytes" << std::endl;

        // Create shader module
        createShaderModule(compCode, &compShaderModule);

        // Setup compute shader stage
        VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
        computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computeShaderStageInfo.module = compShaderModule;
        computeShaderStageInfo.pName = "main";
        computeShaderStageInfo.flags = 0;
        computeShaderStageInfo.pNext = nullptr;
        computeShaderStageInfo.pSpecializationInfo = configInfo.computeSpecializationInfo;

        // Create compute pipeline
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage = computeShaderStageInfo;
        pipelineInfo.layout = configInfo.pipelineLayout;
        pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
        pipelineInfo.basePipelineIndex = -1;
        pipelineInfo.flags = 0;
        pipelineInfo.pNext = nullptr;

        if (vkCreateComputePipelines(engDevice.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create compute pipeline");
        }

        std::cout << "Compute pipeline created successfully" << std::endl;
    }

    void ComputePipeline::bind(VkCommandBuffer commandBuffer)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    }

    /**
     * Execute compute
     */
    void ComputePipeline::dispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
    }

    std::vector<char> ComputePipeline::readFile(const std::string& filepath)
    {
        std::ifstream file{ filepath, std::ios::ate | std::ios::binary };

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filepath);
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        return buffer;
    }

    void ComputePipeline::createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule)
    {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        if (vkCreateShaderModule(engDevice.device(), &createInfo, nullptr, shaderModule) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create compute shader module");
        }
    }

    void ComputePipeline::defaultComputePipelineConfig(ComputePipelineConfig& configInfo)
    {
        // Compute pipelines are much simpler than graphics pipelines
        // Most configuration happens through the pipeline layout and descriptors
        configInfo.computeSpecializationInfo = nullptr;
        
        // Note: pipelineLayout must be set by the caller based on their descriptor layouts
    }

} 