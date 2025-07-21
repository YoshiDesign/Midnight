#pragma once

#include "EngineDevice.h"
#include "GFXPipeline.h"
#include <memory>

namespace aveng {

	class PointLightSystem {

	public:

		PointLightSystem(EngineDevice& device);
		~PointLightSystem();
		void initialize(VkRenderPass renderPass, VkDescriptorSetLayout globalDescriptorSetLayout, VkDescriptorSetLayout lightsDescriptorSetLayout);
		PointLightSystem(const PointLightSystem&) = delete;
		PointLightSystem& operator=(const PointLightSystem&) = delete;
		void render(VkDescriptorSet globalDescriptorSet, VkDescriptorSet lightsDescriptorSet, VkCommandBuffer commandBuffer, int numLights);
		VkPipelineLayout getPipelineLayout() { return pipelineLayout; }

	private:

		void createPipelineLayout(VkDescriptorSetLayout* descriptorSetLayouts);
		void createPipeline(VkRenderPass renderPass);

		EngineDevice& engineDevice;

		// Rendering Pipelines - Heap Allocated
		std::unique_ptr<GFXPipeline> gfxPipeline;
		VkPipelineLayout pipelineLayout;

	};

} 