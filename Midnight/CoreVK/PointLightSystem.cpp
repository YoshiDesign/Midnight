#include <stdexcept>
#include <cassert>
#include "PointLightSystem.h"

namespace aveng {

	PointLightSystem::PointLightSystem(EngineDevice& device, VkRenderData& renderData) 
		: engineDevice{ device }, renderData{ renderData }
	{}

	void PointLightSystem::initialize(VkRenderPass renderPass)
	{
		//VkDescriptorSetLayout descriptorSetLayouts[2] = { 
		//	renderData.rdAvengBasicDescriptorLayout, 
		//	renderData.rdAvengBasicLightingDescriptorLayout};

		//createPipelineLayout(descriptorSetLayouts);
		//createPipeline(renderPass);
	}

	PointLightSystem::~PointLightSystem()
	{
		vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
	}

	/*
	 * Setup of the pipeline layout.
	 * Here we include our Push Constant information
	 * as well as our descriptor set layouts.
	 */
	void PointLightSystem::createPipelineLayout(VkDescriptorSetLayout* descriptorSetLayouts)
	{

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 2;										// How many descriptor set layouts are to be hooked into the pipeline
		pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;						// a pointer to an array of VkDescriptorSetLayout objects.
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;

		// Create the pipeline layout, updating our pipelineLayout member.
		if (vkCreatePipelineLayout(engineDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create pipeline layout!");
		}

	}

	/*
	* Call to the construction of a Graphics Pipeline.
	* Note that shader filepaths are relative to the GFXPipeline.cpp file.
	*/
	void PointLightSystem::createPipeline(VkRenderPass renderPass)
	{
		// Initialize the pipeline 
		assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

		PipelineConfig pipelineConfig{};
		GFXPipeline::defaultPipelineConfig(pipelineConfig);
		pipelineConfig.attributeDescriptions.clear();
		pipelineConfig.bindingDescriptions.clear();
		pipelineConfig.renderPass = renderPass;
		pipelineConfig.pipelineLayout = pipelineLayout;

		// A GFXPipeline
		gfxPipeline = std::make_unique<GFXPipeline>(
			engineDevice,
			"shaders/point_light.vert.spv",
			"shaders/point_light.frag.spv",
			pipelineConfig
		);
	}

	//void PointLightSystem::render(int frameIndex, VkCommandBuffer commandBuffer, int numLights)
	//{

	//}

} 