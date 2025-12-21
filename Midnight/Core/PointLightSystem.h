#pragma once
#include "CoreVK/VkRenderData.h"
#include <memory>
#include <vector>

namespace aveng {

	class EngineDevice;

	class PointLightSystem {

	public:

		PointLightSystem() = delete;
		PointLightSystem(EngineDevice& device, VkRenderData& renderData);
		~PointLightSystem();
		void initialize(VkRenderPass renderPass, int nColorAttachments, bool colorMask);
		// void createDescriptorLayouts();
		PointLightSystem(const PointLightSystem&) = delete;
		PointLightSystem& operator=(const PointLightSystem&) = delete;
		void render(int frameIndex, VkCommandBuffer commandBuffer, int numLights);
		VkPipelineLayout getPipelineLayout() { return pipelineLayout; }
		VkPipeline getPipeline() { return pipeline; }

	private:

		void createPipeline(VkRenderPass renderPass, int nColorAttachments, bool colorMask);

		EngineDevice& engineDevice;

		VkPipelineLayout pipelineLayout;
		VkPipeline pipeline;
		VkRenderData& renderData;

	};

} 