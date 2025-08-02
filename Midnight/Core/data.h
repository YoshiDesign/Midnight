#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <vulkan/vulkan.h>  // For VkVertexInputAttributeDescription
#include "AMD/vk_mem_alloc.h"
#include "CoreVK/EngineDevice.h"

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

	// Data structures moved from ObjectRenderSystem
	struct LightsUbo {
		static constexpr int MAX_LIGHTS = 100;
		uint32_t numLights{ 0 };
		alignas(16) glm::vec4 lightPositions[MAX_LIGHTS];  // w component is radius
		alignas(16) glm::vec4 lightColors[MAX_LIGHTS];     // w component is intensity
	};

	struct GlobalUbo {
		glm::mat4 projection{ 1.f };
		glm::mat4 view{ 1.f };
		glm::vec4 ambientLightColor{ 0.f, 0.f, 1.f, .14f };
		glm::vec3 lightPosition{ 5.0f, -20.0f, 2.8f };
		alignas(16) glm::vec4 lightColor{ 1.f, 1.f, 1.f, 1.f };
		alignas(16) int renderMode{ 0 };  // 0 = STANDARD, 1 = WIREFRAME, 2 = DISTORTED
		alignas(16) float time{ 0.0f };   // For animated effects
	};

	struct ObjectUniformData {
		alignas(16) int texIndex;
	};

	// Instance data for instanced rendering - per object instance
	struct InstanceData {
		alignas(16) glm::mat4 modelMatrix;
		alignas(16) glm::mat4 normalMatrix;
		alignas(16) int textureIndex;
		alignas(16) int padding[3]; // Ensure 16-byte alignment
	};

	/* data format to be uploaded to compute shader - optimized for cache performance */
	struct alignas(64) NodeTransformData {
		glm::vec4 translation = glm::vec4(0.0f);
		glm::vec4 rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // this is a quaternion  
		glm::vec4 scale = glm::vec4(1.0f);
		glm::vec4 padding = glm::vec4(0.0f); // Pad to 64 bytes for cache line alignment
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

		/* Device Data */
		EngineDevice* engineDevice = nullptr;

		glm::vec3 rdCameraView = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdCameraPos  = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdCameraRot  = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdPlayerPos  = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdPlayerRot  = glm::vec3(0.f,0.f,0.f);
		glm::vec3 rdForwardDir = glm::vec3(0.f,0.f,0.f);

		int rdNumPointLights = 0;

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

	// Animation vertex structure with bone weights. Is definitely used
	// FIXED: All vec4 layout eliminates padding issues - perfect 16-byte alignment
	struct AnimatedVertex {
		glm::vec4 position{};     // position.xyz + texCoord.x in .w
		glm::vec4 color{};        // color.xyz + texCoord.y in .w
		glm::vec4 normal{};       // normal.xyz + unused .w (could store tangent.x, etc.)
		
		// Skeletal animation data - naturally 16-byte aligned, no alignas needed!
		glm::ivec4 boneIds{-1, -1, -1, -1};
		glm::vec4 boneWeights{0.0f, 0.0f, 0.0f, 0.0f};
		
		bool operator==(const AnimatedVertex& other) const {
			return position == other.position && 
			       color == other.color && 
			       normal == other.normal && 
			       boneIds == other.boneIds &&
			       boneWeights == other.boneWeights;
		}
	};

	// Transformed vertex structure (output from compute shader) - matches transformed_shader.vert
	struct TransformedVertex {
		glm::vec3 position{};      // Already transformed position
		glm::vec3 color{};         // Color data
		glm::vec3 normal{};        // Already transformed normal
		glm::vec2 texCoord{};      // Texture coordinates
		// NO bone data - transformation already applied!
		
		bool operator==(const TransformedVertex& other) const {
			return position == other.position && 
			       color == other.color && 
			       normal == other.normal && 
			       texCoord == other.texCoord;
		}
		
		// Required for Vulkan pipeline to understand vertex layout
		static std::vector<VkVertexInputBindingDescription> getBindingDescriptions() {
			std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
			bindingDescriptions[0].binding = 0;
			bindingDescriptions[0].stride = sizeof(TransformedVertex);
			bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
			return bindingDescriptions;
		}
		
		static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
			std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
			attributeDescriptions.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TransformedVertex, position) });
			attributeDescriptions.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TransformedVertex, color) });
			attributeDescriptions.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TransformedVertex, normal) });
			attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TransformedVertex, texCoord) });
			return attributeDescriptions;
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
		VkImage image = VK_NULL_HANDLE;
		VkImageView imageView = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		VmaAllocation imageAlloc = nullptr;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
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