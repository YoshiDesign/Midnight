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

}