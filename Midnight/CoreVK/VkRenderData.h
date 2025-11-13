/* Vulkan */
#pragma once

#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>

#include "Utils/glm_includes.h"

#include <vulkan/vulkan.h>
#include "AMD/vk_mem_alloc.h"
#include <GLFW/glfw3.h>

#include <assimp/material.h>

#include "CoreVK/aveng_descriptors.h"
#include "CoreVK/aveng_buffer.h"

namespace aveng {

	//struct Vertex {
	//	// These 4 items get packed into our vertex buffers
	//	glm::vec3 position{};		// Position of the vertex
	//	glm::vec3 color{};			// color at this vertex
	//	glm::vec3 normal{};			// surface norms
	//	glm::vec2 texCoord{};		// 2d texture coordinates

	//	/*
	//	* Required to communicate with the vertex shader.
	//	* Descriptions of our vertex buffers and how they are to be bound.
	//	*/
	//	static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
	//	static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();

	//	// This is used with our hashing function to generate keys in our ordered map of vertices
	//	bool operator==(const Vertex& other) const
	//	{
	//		return position == other.position && color == other.color && normal == other.normal && texCoord == other.texCoord;
	//	}

	//};

	//// Animation vertex structure with bone weights - perfect 16-byte alignment
	//struct AnimatedVertex {
	//	glm::vec4 position{};     // position.xyz + texCoord.x in .w
	//	glm::vec4 color{};        // color.xyz + texCoord.y in .w
	//	glm::vec4 normal{};       // normal.xyz + unused .w (could store tangent.x, etc.)

	//	// Skeletal animation data - naturally 16-byte aligned, no alignas needed!
	//	glm::uvec4 boneNumber = glm::uvec4(0);
	//	glm::vec4 boneWeight = glm::vec4(0.0f);

	//	bool operator==(const AnimatedVertex& other) const {
	//		return position == other.position &&
	//			color == other.color &&
	//			normal == other.normal &&
	//			boneNumber == other.boneNumber &&
	//			boneWeight == other.boneWeight;
	//	}
	//};

	//// Transformed vertex structure (output from compute shader) - matches transformed_shader.vert
	//struct TransformedVertex {
	//	glm::vec3 position{};      // Already transformed position
	//	glm::vec3 color{};         // Color data
	//	glm::vec3 normal{};        // Already transformed normal
	//	glm::vec2 texCoord{};      // Texture coordinates
	//	// NO bone data - transformation already applied!

	//	bool operator==(const TransformedVertex& other) const {
	//		return position == other.position &&
	//			color == other.color &&
	//			normal == other.normal &&
	//			texCoord == other.texCoord;
	//	}

	//	// Required for Vulkan pipeline to understand vertex layout
	//	static std::vector<VkVertexInputBindingDescription> getBindingDescriptions() {
	//		std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
	//		bindingDescriptions[0].binding = 0;
	//		bindingDescriptions[0].stride = sizeof(TransformedVertex);
	//		bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	//		return bindingDescriptions;
	//	}

	//	static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
	//		std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
	//		attributeDescriptions.push_back({ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TransformedVertex, position) });
	//		attributeDescriptions.push_back({ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TransformedVertex, color) });
	//		attributeDescriptions.push_back({ 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TransformedVertex, normal) });
	//		attributeDescriptions.push_back({ 3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(TransformedVertex, texCoord) });
	//		return attributeDescriptions;
	//	}
	//};

	struct LightsUbo {
		static constexpr int MAX_LIGHTS = 100;
		uint32_t numLights{ 0 };
		alignas(16) glm::vec4 lightPositions[MAX_LIGHTS];  // w component is radius
		alignas(16) glm::vec4 lightColors[MAX_LIGHTS];     // w component is intensity
	};

	// NOTE: Recall Dynamic UBOs (see Renderer::calculateDynamicUBOStride) Change it to whatever you need
	struct ObjectUniformData {
		alignas(16) int texIndex;
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
		unsigned int bufferSize = 0;
		void* data = nullptr;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation bufferAlloc = VK_NULL_HANDLE;
		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VmaAllocation stagingBufferAlloc = VK_NULL_HANDLE;
	};

	struct VkIndexBufferData {
		size_t bufferSize = 0;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation bufferAlloc = nullptr;
		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VmaAllocation stagingBufferAlloc = nullptr;
	};

	struct VkUniformBufferData {
		size_t bufferSize = 0;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation bufferAlloc = nullptr;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};

	struct VkShaderStorageBufferData {
		size_t bufferSize = 0;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation bufferAlloc = nullptr;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};

	struct VkLineVertex {
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 color = glm::vec3(0.0f);
	};

	struct VkLineMesh {
		std::vector<VkLineVertex> vertices{};
	};

	struct VkVertex {
		glm::vec4 position = glm::vec4(0.0f);	// last float is uv.x
		glm::vec4 color = glm::vec4(1.0f);
		glm::vec4 normal = glm::vec4(0.0f);		// last float is uv.y
		glm::uvec4 boneNumber = glm::uvec4(0);
		glm::vec4 boneWeight = glm::vec4(0.0f);
	};

	struct VkMesh {
		std::vector<VkVertex> vertices{};
		std::vector<uint32_t> indices{};
		std::unordered_map<aiTextureType, std::string> textures{};
		bool usesPBRColors = false;
	};

/* data format to be uploaded to compute shader */
	struct NodeTransformData {
		glm::vec4 translation = glm::vec4(0.0f);
		glm::vec4 scale = glm::vec4(1.0f);
		glm::vec4 rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // this is a quaternion
	};

	struct VkUploadMatrices {
		alignas(16) glm::mat4 viewMatrix{};
		alignas(16) glm::mat4 projectionMatrix{};
	};

	struct VkPushConstants {
		uint32_t pkModelStride;
		uint32_t pkWorldPosOffset;
		uint32_t pkSkinMatOffset;
	};

	struct VkComputePushConstants {
		uint32_t pkModelOffset;
	};

	enum class instanceEditMode : uint8_t {
		move = 0,
		rotate,
		scale
	};

	struct VkRenderData {

		/**
		* Here you'll find many of the resources required to operate the vulkan api.
		* However, the EngineDevice still maintains these responsibilities: 
			- command buffer operations
			- queues
			- command pools
		*/

		GLFWwindow* rdWindow = nullptr;

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

		int rdMoveForward = 0;
		int rdMoveRight = 0;
		int rdMoveUp = 0;

		float rdViewAzimuth = 330.0f;
		float rdViewElevation = -20.0f;
		glm::vec3 rdCameraWorldPosition = glm::vec3(2.0f, 5.0f, 7.0f);

		/**
		* Command buffers
		*/
		//VkCommandPool rdCommandPool = VK_NULL_HANDLE;			// Get from engine device
		//VkCommandPool rdComputeCommandPool = VK_NULL_HANDLE;	// Get from engine device
		std::vector<VkCommandBuffer> rdCommandBuffersGraphics;
		std::vector<VkCommandBuffer> rdCommandBuffersCompute;
		std::vector<VkCommandBuffer> rdLineCommandBuffers;		// EDITOR

		VkRenderPass rdLineRenderpass;							// EDITOR
		VkRenderPass rdSelectionRenderpass;							// EDITOR

		/**
		* Sync
		*/
		std::vector<VkSemaphore> rdPresentSemaphore;
		std::vector<VkSemaphore>rdRenderSemaphore;
		std::vector<VkSemaphore>rdGraphicSemaphore;
		std::vector<VkSemaphore>rdComputeSemaphore;
		std::vector<VkFence>rdRenderFence;
		std::vector<VkFence>rdComputeFence;

		/*
		* Descriptors
		*/
		VkDescriptorPool avengDescriptorPool = VK_NULL_HANDLE;

		VkDescriptorSetLayout rdAvengDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengAnimationDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengTextureDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengComputeTransformDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengComputeMatrixMultDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengComputeMatrixMultPerModelDescriptorLayout = VK_NULL_HANDLE;

		std::vector<VkDescriptorSet> rdAvengDescriptorSets;
		std::vector<VkDescriptorSet> rdAvengAnimationDescriptorSets;
		std::vector<VkDescriptorSet> rdAvengComputeTransformDescriptorSets;
		std::vector<VkDescriptorSet> rdAvengComputeMatrixMultDescriptorSets;
		std::vector<VkDescriptorSet> basicLightingDescriptorSets;
		std::vector<VkDescriptorSet> textureDescriptorSets;
		std::vector<VkDescriptorSet> rdLineDescriptorSets;			// EDITOR

		/*
		* Pipeline
		*/
		VkPipelineLayout rdAvengPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengAnimationPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengSelectionPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengSkinningSelectionPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdLinePipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengComputeTransformPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengComputeMatrixMultPipelineLayout = VK_NULL_HANDLE;

		VkPipeline rdAvengPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengAnimationPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengSelectionPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengSkinningSelectionPipeline = VK_NULL_HANDLE;
		VkPipeline rdLinePipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengComputeTransformPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengComputeMatrixMultPipeline = VK_NULL_HANDLE;

		//VkDescriptorPool rdImguiDescriptorPool = VK_NULL_HANDLE;

		/*
		* Editor Data
		*/
		instanceEditMode rdInstanceEditMode = instanceEditMode::move;
	};
}