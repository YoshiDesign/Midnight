/* model specific settings */
#pragma once

#include <vector>
#include <memory>
#include "Utils/glm_includes.h"

namespace aveng {
	struct InstanceSettings {
		glm::vec3 isWorldPosition = glm::vec3(0.0f);
		glm::vec3 isWorldRotation = glm::vec3(0.0f);
		float isScale = 1.0f;
		bool isSwapYZAxis = false;

		int isInstanceIndexPosition = -1;

		size_t isAnimClipSize = 0;
		unsigned int isAnimClipNr = 0;
		float isAnimPlayTimePos = 0.0f;
		float isAnimSpeedFactor = 1.0f;
	};

}