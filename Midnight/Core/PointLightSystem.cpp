#include <stdexcept>
#include <cassert>
#include <iostream>
#include "Utils/Logger.h"
#include "PointLightSystem.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/PipelineLayout.h"
#include "CoreVK/LightPipeline.h"

namespace aveng {

	PointLightSystem::PointLightSystem(EngineDevice& device, VkRenderData& renderData) 
		: engineDevice{ device }, renderData{ renderData }
	{}

	// Initialize point light system using existing descriptor set layouts
	void PointLightSystem::initialize(VkRenderPass renderPass, int nColorAttachments, bool colorMask)
	{
		// Important: Using rdAvengDescriptorLayout here which means 
		std::vector<VkDescriptorSetLayout> layouts = { renderData.rdAvengDescriptorLayout };
		PipelineLayout::init(engineDevice, pipelineLayout, layouts);
		createPipeline(renderPass, nColorAttachments, colorMask);
	}

	PointLightSystem::~PointLightSystem()
	{
		std::cout << "Destroying PointLight System" << std::endl;
		vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
		vkDestroyPipeline(engineDevice.device(), pipeline, nullptr);
	}

	void PointLightSystem::createPipeline(VkRenderPass renderPass, int nColorAttachments, bool colorMask)
	{
		std::string vertexShaderFile = "shaders/point_light.vert.spv";
		std::string fragmentShaderFile = "shaders/point_light.frag.spv";

		if (!LightPipeline::init(engineDevice, pipelineLayout, pipeline, renderPass, 
			nColorAttachments, colorMask, vertexShaderFile, fragmentShaderFile)) 
		{
			Logger::log(1, "%s error: could not init aveng point light shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("point light system fail 1");
		}
	}

	void PointLightSystem::render(int frameIndex, VkCommandBuffer commandBuffer, int numLights)
	{

	}

} 