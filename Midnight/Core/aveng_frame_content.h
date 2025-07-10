#pragma once

#include "System/Camera/aveng_camera.h"
#include "Core/app_object.h"

namespace aveng {
	struct FrameContent {

		//int frameIndex;
		float frameTime;
		//VkCommandBuffer commandBuffer;
		AvengCamera& camera;
		//VkDescriptorSet globalDescriptorSet;
		//VkDescriptorSet objectDescriptorSet;
		AvengAppObject::Map& appObjects;

	};
}