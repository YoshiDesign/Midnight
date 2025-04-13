#include "PointLightSystem.h"
#include "../Math/aveng_math.h"
#include "../Events/window_callbacks.h"
#include "../Player/GameplayFunctions.h"

#define exe GameplayFunctions

#define LOG(x, y) std::cout << x << "\t" << y << std::endl
#define BYPASS_FBO 0

namespace aveng {

	PointLightSystem::PointLightSystem(EngineDevice& device) : engineDevice{ device } 
	{}

	void PointLightSystem::initialize(VkRenderPass renderPass, VkDescriptorSetLayout globalDescriptorSetLayouts)
	{
		VkDescriptorSetLayout descriptorSetLayouts[1] = { globalDescriptorSetLayouts };
		createPipelineLayout(descriptorSetLayouts);
		createPipeline(renderPass);
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

		// Initialize push constant range(s)
		//VkPushConstantRange pushConstantRange{};
		//pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		//pushConstantRange.offset = 0;
		//pushConstantRange.size = sizeof(SimplePushConstantData);	// Must be a multiple of 4

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 1;										// How many descriptor set layouts are to be hooked into the pipeline
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

		// Another GFXPipeline
		//gfxPipeline2 = std::make_unique<GFXPipeline>(
		//	engineDevice,
		//	"shaders/simple_shader2.vert.spv",
		//	"shaders/simple_shader2.frag.spv",
		//	pipelineConfig
		//	);
	}

	void PointLightSystem::render(VkDescriptorSet globalDescriptorSet, VkCommandBuffer commandBuffer)
	{

		gfxPipeline->bind(commandBuffer); // 0
	
		vkCmdBindDescriptorSets(
			commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0,
			1,
			&globalDescriptorSet,
			0,
			nullptr);

		vkCmdDraw(commandBuffer, 6, 1, 0, 0);

	}

} //