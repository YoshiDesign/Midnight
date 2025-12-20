#pragma once
#include "CoreVK/VkRenderData.h"
#include <memory>

namespace aveng {

	class EngineDevice;

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

	private:

		void createPipelineLayout(VkDescriptorSetLayout* descriptorSetLayouts);
		void createPipeline(VkRenderPass renderPass);

		EngineDevice& engineDevice;

		VkPipelineLayout pipelineLayout;
		VkRenderData& renderData;

	};

} 