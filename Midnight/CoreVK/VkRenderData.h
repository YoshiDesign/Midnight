/* Vulkan */
#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

#include <glm/glm.hpp>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <assimp/material.h>
#include "CoreVK/EngineDevice.h"
namespace aveng {

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
		glm::vec4 ambientLightColor{ 0.f, 0.f, 0.89f, .05f };
		glm::vec3 lightPosition{ 5.0f, -20.0f, 2.8f };
		alignas(16) glm::vec4 lightColor{ 1.f, 1.f, 1.f, 1.f };
		alignas(16) int renderMode{ 0 };  // 0 = STANDARD, 1 = WIREFRAME, 2 = DISTORTED
		alignas(16) float time{ 0.0f };   // For animated effects
	};

	struct ObjectUniformData {
		alignas(16) int texIndex;
	};

	/* data format to be uploaded to compute shader - optimized for cache performance */
	struct alignas(64) NodeTransformData {
		glm::vec4 translation = glm::vec4(0.0f);
		glm::vec4 rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // this is a quaternion  
		glm::vec4 scale = glm::vec4(1.0f);
		glm::vec4 padding = glm::vec4(0.0f); // Pad to 64 bytes for cache line alignment
	};

	struct Vertex {
		// These 4 items get packed into our vertex buffers
		glm::vec3 position{};		// Position of the vertex
		glm::vec3 color{};			// color at this vertex
		glm::vec3 normal{};			// surface norms
		glm::vec2 texCoord{};		// 2d texture coordinates

		/*
		* Required to communicate with the vertex shader.
		* Descriptions of our vertex buffers and how they are to be bound.
		*/
		static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
		static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

		// This is used with our hashing function to generate keys in our ordered map of vertices
		bool operator==(const Vertex& other) const
		{
			return position == other.position && color == other.color && normal == other.normal && texCoord == other.texCoord;
		}

	};

	// Instance data for instanced rendering - per object instance
	struct InstanceData {
		alignas(16) glm::mat4 modelMatrix;
		alignas(16) glm::mat4 normalMatrix;
		alignas(16) int textureIndex;
		alignas(16) int padding[3]; // Ensure 16-byte alignment
	};

	// Animation vertex structure with bone weights - perfect 16-byte alignment
	struct AnimatedVertex {
		glm::vec4 position{};     // position.xyz + texCoord.x in .w
		glm::vec4 color{};        // color.xyz + texCoord.y in .w
		glm::vec4 normal{};       // normal.xyz + unused .w (could store tangent.x, etc.)

		// Skeletal animation data - naturally 16-byte aligned, no alignas needed!
		glm::uvec4 boneNumber = glm::uvec4(0);
		glm::vec4 boneWeight = glm::vec4(0.0f);

		bool operator==(const AnimatedVertex& other) const {
			return position == other.position &&
				color == other.color &&
				normal == other.normal &&
				boneNumber == other.boneNumber &&
				boneWeight == other.boneWeight;
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

	struct VkUniformBufferData {
		size_t bufferSize = 0;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation bufferAlloc = nullptr;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};

	struct VkShaderStorageBufferData {
		// Placeholder for actual Vulkan SSBO
		size_t bufferSize = 0;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation bufferAlloc = nullptr;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};

	// Instance animation state for SSBO
	struct InstanceAnimationData {
		alignas(16) glm::mat4 modelMatrix{ 1.0f };           // Instance transform
		alignas(4) float animationTime{ 0.0f };              // Current animation time
		alignas(4) int animationClipIndex{ 0 };              // Which animation to play
		alignas(4) int boneMatrixOffset{ 0 };                // Offset into bone matrix buffer
		alignas(4) int boneCount{ 0 };                       // Number of bones for this instance
		alignas(16) glm::vec4 animationParams{ 1.0f, 0.0f, 0.0f, 0.0f }; // speed, loop, etc.
	};

	// Global uniform data for animation compute shader
	struct AnimationComputeUbo {
		alignas(4) float deltaTime{ 0.0f };                  // Frame delta time
		alignas(4) uint32_t totalInstances { 0 };            // Number of animated instances
		alignas(4) uint32_t maxBonesPerInstance { 128 };     // Maximum bones per model
		alignas(4) uint32_t verticesPerInstance { 0 };       // Vertices to process per instance
		alignas(16) glm::vec4 debugParams{ 0.0f };          // Debug/experimental parameters
	};

	// Animation-related UBOs for descriptor sets
	struct AnimationUbo {
		AnimationComputeUbo computeData;
		alignas(16) glm::mat4 reserved[4];                 // Reserved for future expansion
	};

	struct VkVertex {
		glm::vec4 position = glm::vec4(0.0f); // last float is uv.x
		glm::vec4 color = glm::vec4(1.0f);
		glm::vec4 normal = glm::vec4(0.0f); // last float is uv.y
		glm::uvec4 boneNumber = glm::uvec4(0);
		glm::vec4 boneWeight = glm::vec4(0.0f);
	};

	struct VkMesh {
		std::vector<VkVertex> vertices{};
		std::vector<uint32_t> indices{};
		std::unordered_map<aiTextureType, std::string> textures{};
		bool usesPBRColors = false;
	};

	struct VkUploadMatrices {
		glm::mat4 viewMatrix{};
		glm::mat4 projectionMatrix{};
	};

	struct VkPushConstants {
		uint32_t pkModelStride;
		uint32_t pkWorldPosOffset;
		uint32_t pkSkinMatOffset;
	};

	struct VkComputePushConstants {
		uint32_t pkModelOffset;
	};

	struct VkRenderData {

		GLFWwindow* rdWindow = nullptr;

		

		bool rdHasDedicatedComputeQueue = false;

		int rdWidth = 0;
		int rdHeight = 0;

		unsigned int rdTriangleCount = 0;
		unsigned int rdMatricesSize = 0;

		int rdFieldOfView = 60;

		float rdFrameTime = 0.0f;
		float rdMatrixGenerateTime = 0.0f;
		float rdUploadToVBOTime = 0.0f;
		float rdUploadToUBOTime = 0.0f;
		float rdUIGenerateTime = 0.0f;
		float rdUIDrawTime = 0.0f;

		// Command Buffers
		VkCommandBuffer rdCommandBufferGraphics = VK_NULL_HANDLE;
		VkCommandBuffer rdCommandBufferCompute = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> rdCommandBuffersGraphics;
		std::vector<VkCommandBuffer> rdCommandBuffersCompute;


		int rdMoveForward = 0;
		int rdMoveRight = 0;
		int rdMoveUp = 0;

		float rdViewAzimuth = 330.0f;
		float rdViewElevation = -20.0f;
		glm::vec3 rdCameraWorldPosition = glm::vec3(2.0f, 5.0f, 7.0f);

		std::vector<VkImage> rdSwapchainImages{};
		std::vector<VkImageView> rdSwapchainImageViews{};
		std::vector<VkFramebuffer> rdFramebuffers{};

		//VkQueue rdGraphicsQueue = VK_NULL_HANDLE;
		//VkQueue rdPresentQueue = VK_NULL_HANDLE;
		//VkQueue rdComputeQueue = VK_NULL_HANDLE;

		//VkImage rdDepthImage = VK_NULL_HANDLE;
		//VkImageView rdDepthImageView = VK_NULL_HANDLE;
		//VkFormat rdDepthFormat = VK_FORMAT_UNDEFINED;

		//VkRenderPass rdRenderpass = VK_NULL_HANDLE;

		VkPipelineLayout rdAvengPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengSkinningPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengComputeTransformaPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengComputeMatrixMultPipelineLayout = VK_NULL_HANDLE;

		VkPipeline rdAvengPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengSkinningPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengComputeTransformPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengComputeMatrixMultPipeline = VK_NULL_HANDLE;

		VkCommandPool rdCommandPool = VK_NULL_HANDLE;
		VkCommandPool rdComputeCommandPool = VK_NULL_HANDLE;
		VkCommandBuffer rdCommandBuffer = VK_NULL_HANDLE;
		VkCommandBuffer rdComputeCommandBuffer = VK_NULL_HANDLE;

		VkSemaphore rdPresentSemaphore = VK_NULL_HANDLE;
		VkSemaphore rdRenderSemaphore = VK_NULL_HANDLE;
		VkSemaphore rdGraphicSemaphore = VK_NULL_HANDLE;
		VkSemaphore rdComputeSemaphore = VK_NULL_HANDLE;
		VkFence rdRenderFence = VK_NULL_HANDLE;
		VkFence rdComputeFence = VK_NULL_HANDLE;

		VkDescriptorSetLayout rdAvengDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengSkinningDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengTextureDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengComputeTransformDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengComputeMatrixMultDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengComputeMatrixMultPerModelDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengBasicLightingDescriptorLayout = VK_NULL_HANDLE;

		VkDescriptorSet rdAvengDescriptorSet = VK_NULL_HANDLE;
		VkDescriptorSet rdAvengSkinningDescriptorSet = VK_NULL_HANDLE;
		VkDescriptorSet rdAvengComputeTransformDescriptorSet = VK_NULL_HANDLE;
		VkDescriptorSet rdAvengComputeMatrixMultDescriptorSet = VK_NULL_HANDLE;

		VkDescriptorPool avengDescriptorPool = VK_NULL_HANDLE;

		//VkDescriptorPool rdImguiDescriptorPool = VK_NULL_HANDLE;
	};
}