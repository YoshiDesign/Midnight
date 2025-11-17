#pragma once
#include "Utils/glm_includes.h"
#include <string>
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>  // For VkVertexInputAttributeDescription
#include "AMD/vk_mem_alloc.h"
#include "CoreVK/EngineDevice.h"
//#include <unordered_map>
//#include "aveng_model.h"

namespace aveng {
	const int RIGHT = -1;
	const int LEFT = 1;
	const float PI = 3.14159265f; // 3589793238462643383279502884197969399375105820974944592 check yo'self
	const float viewRadius{ .5f };	// Radius of the invisible sphere for which our viewer is at the origin

	//std::unordered_map < std::string , AvengModel > models_in_scene;

	// Used by Components System
	enum types {
		GROUND = 0,
		PLAYER,
		ENEMY,
		SCENE,
		STATIC,
		DYNAMIC
	};

	// Debug info for GUI, also contains player state data ...oops
	struct GameData {
		int			numPointLights = 0;
		int			num_objs;
		float		dt;
		int			cur_pipe;
		int			sec;
		int			pn;
		glm::vec3	cameraView;
		glm::vec3	cameraPos;
		glm::vec3	cameraRot;
		glm::vec3	playerPos;
		glm::vec3	playerRot;
		glm::vec3	forwardDir;
		glm::vec3	modRot;
		glm::vec3	modPos;
		float		modPI{PI};
		float		player_modPI;
		float		camera_modPI;

		// From Keyboard Controller
		bool		fly_mode = false;
		float		DPI;
		float		DeltaRoll;
		float		player_z_rot;
		float		cameraDX;
		float		speed;
		glm::vec3	velocity;

	};

	struct ModelData {

		// General Model Data
		int mdTotalVertices = 0;
		int mdTotalTriangles = 0;
		int mdTotalModels = 0;
		int mdActiveModels = 0;

		// Model Animation Data
		int mdAnimatedModels = 0;
		int mdTotalBones = 0;
		int mdTotalNodes = 0;
		int mdTotalAnimationClips = 0;
		float mdAnimationUpdateTime = 0.0f;
	};

	
	struct RenderData {  // DEPRECATED

		int   rdMatricesSize = 0;
		float rdUploadToUBOTime = 0.0f;
		float rdUploadToVBOTime = 0.0f;
		float rdMatrixGenerateTime = 0.0f;
		float rdUIGenerateTime = 0.0f;
		float rdUIDrawTime = 0.0f;
		float rdFrameTime = 0.0f;

		int rdMoveForward = 0;
		int rdMoveRight = 0;
		int rdMoveUp = 0;

		/* Device Data */
		EngineDevice* engineDevice = nullptr;

		glm::vec3 rdCameraView = glm::vec3(0.f, 0.f, 0.f);
		glm::vec3 rdCameraPos = glm::vec3(0.f, 0.f, 0.f);
		glm::vec3 rdCameraRot = glm::vec3(0.f, 0.f, 0.f);
		glm::vec3 rdPlayerPos = glm::vec3(0.f, 0.f, 0.f);
		glm::vec3 rdPlayerRot = glm::vec3(0.f, 0.f, 0.f);
		glm::vec3 rdForwardDir = glm::vec3(0.f, 0.f, 0.f);

		int rdCurrentPipelineMode = 0; // 0=static, 1=animated, etc.
	};

	// Pipeline management enums
	enum class ObjectRenderMode {
		STANDARD = 0,
		WIREFRAME = 1,
		DISTORTED = 2,
		// Add more as needed
	};

	enum class PostProcessMode {
		NONE = 0,
		TOXIC_CLOUD = 1,
		NIGHT_VISION = 2,
		CHROMATIC_ABERRATION = 3,
		// Add more effects
	};


}