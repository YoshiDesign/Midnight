#pragma once

#include "Core/app_object.h"
#include "Core/aveng_frame_content.h"
#include "System/Camera/aveng_camera.h"
#include "Core/aveng_window.h"
#include "System/Peripheral/KeyboardController.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/GFXPipeline.h"
#include "Core/data.h"

#include "avpch.h"

namespace aveng {

	class PointLightSystem {

	public:

		//struct FragUbo {
		//	alignas(sizeof(int)) int imDex;
		//};

		PointLightSystem(EngineDevice& device);
		~PointLightSystem();
		void initialize(VkRenderPass renderPass, VkDescriptorSetLayout globalDescriptorSetLayout);
		PointLightSystem(const PointLightSystem&) = delete;
		PointLightSystem& operator=(const PointLightSystem&) = delete;
		void render(VkDescriptorSet descriptorSet, VkCommandBuffer commandBuffer);
		VkPipelineLayout getPipelineLayout() { return pipelineLayout; }

	private:

		void createPipelineLayout(VkDescriptorSetLayout* descriptorSetLayouts);
		void createPipeline(VkRenderPass renderPass);

		int last_sec;
		EngineDevice& engineDevice;
		size_t deviceAlignment = engineDevice.properties.limits.minUniformBufferOffsetAlignment;

		// Rendering Pipelines - Heap Allocated
		std::unique_ptr<GFXPipeline> gfxPipeline;
		std::unique_ptr<GFXPipeline> gfxPipeline2;
		VkPipelineLayout pipelineLayout;

	};

}