#pragma once

#include "EngineDevice.h"

#include <string>
#include <vector>

/**
* @class GFXPipeline
* Used to implement our precompiled shaders
*/
namespace aveng {

	// This struct is used by our Application code to be able to 
	// completely configure our graphics pipeline config.
	// For this reason, we include it here, instead of in GFXPipeline
	// This way we can share this config between multiple pipelines
	struct PipelineConfig {
		PipelineConfig() = default;
		PipelineConfig(const PipelineConfig&) = delete;
		PipelineConfig& operator=(const PipelineConfig&) = delete;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
		std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
		VkPipelineViewportStateCreateInfo viewportInfo;
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
		VkPipelineRasterizationStateCreateInfo rasterizationInfo;
		VkPipelineMultisampleStateCreateInfo multisampleInfo;
		VkPipelineColorBlendAttachmentState colorBlendAttachment;
		VkPipelineColorBlendStateCreateInfo colorBlendInfo;
		VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
		std::vector<VkDynamicState> dynamicStateEnables;
		VkPipelineDynamicStateCreateInfo dynamicStateInfo;
		VkPipelineLayout pipelineLayout = nullptr;
		VkRenderPass renderPass = nullptr;
		uint32_t subpass = 0;
	};
	
	/**
	* The Vulkan Graphics Pipeline implementation
	*/
	class GFXPipeline {
		// @warn - A reference member var to our engine device. If we have a GFX Pipeline, the necessity of an engine device is implicit.
		// You should never copy a GFXPipeline
		EngineDevice& engDevice;
		VkPipeline graphicsPipeline;		// typedef pointer
		VkShaderModule vertShaderModule;	// typedef pointer
		VkShaderModule fragShaderModule;	// typedef pointer

	public:

		GFXPipeline(
			EngineDevice& device, 
			const std::string& vertFilepath, 
			const std::string& fragFilepath, 
			const PipelineConfig& config
		);

		~GFXPipeline();
		GFXPipeline(const GFXPipeline&) = delete;
		GFXPipeline& operator=(const GFXPipeline&) = delete;
		void bind(VkCommandBuffer commandBuffer);
		static void defaultPipelineConfig(PipelineConfig& configInfo);

	private:

		// These are both public now...
		static std::vector<char> readFile(const std::string& filepath);
		void createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule);
		void createGFXPipeline(const std::string& vertFilepath, const std::string& fragFilepath, const PipelineConfig& config);

	};


} // NS