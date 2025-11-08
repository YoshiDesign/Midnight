#pragma once

#include "EngineDevice.h"
#include "GFXPipeline.h"
#include "CoreVK/VkRenderData.h"
#include <memory>

namespace aveng {

	class PointLightSystem {

	public:

		PointLightSystem() = delete;
		PointLightSystem(EngineDevice& device, VkRenderData& renderData);
		~PointLightSystem();
		void initialize(VkRenderPass renderPass);
		PointLightSystem(const PointLightSystem&) = delete;
		PointLightSystem& operator=(const PointLightSystem&) = delete;
		void render(int frameIndex, VkCommandBuffer commandBuffer, int numLights);
		VkPipelineLayout getPipelineLayout() { return pipelineLayout; }
		std::unique_ptr<GFXPipeline>& getPipeline(){ return gfxPipeline; }

	private:

		void createPipelineLayout(VkDescriptorSetLayout* descriptorSetLayouts);
		void createPipeline(VkRenderPass renderPass);

		EngineDevice& engineDevice;

		// Rendering Pipelines - Heap Allocated
		std::unique_ptr<GFXPipeline> gfxPipeline;
		VkPipelineLayout pipelineLayout;
		VkRenderData& renderData;

	};

} 