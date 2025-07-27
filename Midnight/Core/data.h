#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <string>
#include <vector>

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

		// Animation system debug data
		int rdLoadedModels = 0;
		int rdAnimatedModels = 0;
		int rdTotalBones = 0;
		int rdTotalNodes = 0;
		int rdTotalAnimationClips = 0;
		int rdActiveInstances = 0;
		float rdAnimationUpdateTime = 0.0f;
		int rdCurrentPipelineMode = 0; // 0=static, 1=animated, etc.
	};

	// Animation vertex structure with bone weights
	struct AnimatedVertex {
		glm::vec3 position{};
		glm::vec3 color{};
		glm::vec3 normal{};
		glm::vec2 texCoord{};
		
		// Skeletal animation data - up to 4 bone influences per vertex
		alignas(16) glm::ivec4 boneIds{-1, -1, -1, -1};
		alignas(16) glm::vec4 boneWeights{0.0f, 0.0f, 0.0f, 0.0f};
		
		bool operator==(const AnimatedVertex& other) const {
			return position == other.position && 
			       color == other.color && 
			       normal == other.normal && 
			       texCoord == other.texCoord &&
			       boneIds == other.boneIds &&
			       boneWeights == other.boneWeights;
		}
	};

	// Placeholder mesh and buffer types for animation system
	struct VkMesh {
		std::vector<AnimatedVertex> vertices{};
		std::vector<uint32_t> indices{};
		std::string name{};
		bool hasAnimationData = false;
	};

	struct VkTextureData {
		std::string texturePath{};
		// Placeholder for actual Vulkan texture data
		bool isLoaded = false;
	};

	struct VkVertexBufferData {
		// Placeholder for actual Vulkan vertex buffer
		size_t bufferSize = 0;
		bool isCreated = false;
	};

	struct VkIndexBufferData {
		// Placeholder for actual Vulkan index buffer  
		size_t bufferSize = 0;
		bool isCreated = false;
	};

	struct VkShaderStorageBufferData {
		// Placeholder for actual Vulkan SSBO
		size_t bufferSize = 0;
		bool isCreated = false;
	};

	// === ANIMATION SSBO DATA STRUCTURES ===
	
	// Instance animation state for SSBO
	struct InstanceAnimationData {
		alignas(16) glm::mat4 modelMatrix{1.0f};           // Instance transform
		alignas(4) float animationTime{0.0f};              // Current animation time
		alignas(4) int animationClipIndex{0};              // Which animation to play
		alignas(4) int boneMatrixOffset{0};                // Offset into bone matrix buffer
		alignas(4) int boneCount{0};                       // Number of bones for this instance
		alignas(16) glm::vec4 animationParams{1.0f, 0.0f, 0.0f, 0.0f}; // speed, loop, etc.
	};

	// Global uniform data for animation compute shader
	struct AnimationComputeUbo {
		alignas(4) float deltaTime{0.0f};                  // Frame delta time
		alignas(4) uint32_t totalInstances{0};            // Number of animated instances
		alignas(4) uint32_t maxBonesPerInstance{128};     // Maximum bones per model
		alignas(4) uint32_t verticesPerInstance{0};       // Vertices to process per instance
		alignas(16) glm::vec4 debugParams{0.0f};          // Debug/experimental parameters
	};

	// Animation-related UBOs for descriptor sets
	struct AnimationUbo {
		AnimationComputeUbo computeData;
		alignas(16) glm::mat4 reserved[4];                 // Reserved for future expansion
	};

}