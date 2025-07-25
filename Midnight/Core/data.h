#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace aveng {
	const int RIGHT = -1;
	const int LEFT = 1;
	const int PI = 3.14159265f; // 3589793238462643383279502884197969399375105820974944592 check yo'self
	const float viewRadius{ .5f };	// Radius of the invisible sphere for which our viewer is at the origin

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

	/* data format to be uploaded to compute shader */
	struct NodeTransformData {
		glm::vec4 translation = glm::vec4(0.0f);
		glm::vec4 scale = glm::vec4(1.0f);
		glm::vec4 rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // this is a quaternion
	};

	struct RenderData {

		int   rdMatricesSize = 0;
		float rdUploadToUBOTime = 0.0f;
		float rdUploadToVBOTime = 0.0f;
		float rdMatrixGenerateTime = 0.0f;
		float rdUIGenerateTime = 0.0f;
		float rdUIDrawTime = 0.0f;
		float rdFrameTime = 0.0f;

		int rdMoveForward = 0;
		int rdMoveRight   = 0;
		int rdMoveUp	  = 0;

		glm::vec3 rdCameraView = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdCameraPos  = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdCameraRot  = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdPlayerPos  = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdPlayerRot  = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdForwardDir = glm::vec3(0.f,0.f,0.f);
	};

}