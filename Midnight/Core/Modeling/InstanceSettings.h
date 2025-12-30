/* model specific settings */
#pragma once

#include <vector>
#include <memory>
#include "Utils/glm_includes.h"

namespace aveng {
    
    /* The instance's copy of its settings */
    struct InstanceTransform {
        glm::vec3 pos{ 0.f };
        glm::vec3 rotEuler{ 0.f };   // keep for now; later consider quat
        float     scale{ 1.f };
    };

    struct AnimSettings {
        size_t animClipSize = 0;
        uint32_t clipNr{ 0 };
        float    playTime{ 0.f }; // Somewhere between 0 and duration
        float    speed{ 1.f };
    };

    /* */
    struct TransformSettings {
        glm::vec3 worldPosition{ 0.f };
        glm::vec3 worldRotation{ 0.f };
        float     scale{ 1.f };
        bool      swapYZ{ false };
    };

    /* */
    struct AnimatedCreateSettings {
        TransformSettings transform;
        AnimSettings      anim;
    };

	//struct InstanceSettings {
	//	glm::vec3 isWorldPosition = glm::vec3(0.0f);
	//	glm::vec3 isWorldRotation = glm::vec3(0.0f);
	//	float isScale = 1.0f;
	//	bool isSwapYZAxis = false;

	//	int isInstanceIndexPosition = -1;

	//	size_t isAnimClipSize = 0;
	//	unsigned int isAnimClipNr = 0;
	//	float isAnimPlayTimePos = 0.0f;
	//	float isAnimSpeedFactor = 1.0f;
	//};

}