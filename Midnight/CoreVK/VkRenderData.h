/* Vulkan */
#pragma once


/**
* BIG TODO : Remove anything that's a std::vector from VkRenderData.
*			 Doing so will make things waaaaay more cache friendly.
*			 The Renderer class can own most of the things stored in vec's.
*/

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

#ifndef WTF_BOOM
#define WTF_BOOM 9002
#endif

namespace aveng {

	enum class MapMode { OnDemand, Persistent, GpuOnly };
	enum class ResidentMode { CPU, GPU };

	// std::span<T> super-lightweight doppleganger. Used to create a window into our data
	// so the Editor can access buffers.
	// Don't forget that the same mapped buffer rules apply: don't randomly index into buffers with VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
	template <typename T>
	struct Span {
		const T* data = nullptr;
		size_t   size = 0;

		const T& operator[](size_t i) const { return data[i]; }
		bool empty() const { return size == 0; }
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
		void* mapped = nullptr; // Used when persistently mapped
		bool isHostCoherent = false; // true == no need to flush

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE; // Unused
	};

	struct VkShaderStorageBufferData {
		size_t bufferSize = 0;
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation bufferAlloc = nullptr;
		void* mapped = nullptr;
		bool isHostCoherent = false;

		VkDescriptorSet descriptorSet = VK_NULL_HANDLE; // Unused
	};

	struct PendingBufferDestroy {
		VkBuffer			buffer;
		VmaAllocation		allocation;
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

	struct AvengCameraProxy {
		glm::mat4 projection;
		glm::mat4 view;
	};

/* data format to be uploaded to compute shader */
	struct NodeTransformData {
		glm::vec4 translation = glm::vec4(0.0f);
		glm::vec4 scale = glm::vec4(1.0f);
		glm::vec4 rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); // this is a "quaternion"
	};

	struct PointLightData {
		glm::vec4 ambientLightColor;	// w is intensity
		glm::vec4 positions[200];		// w is radius
		glm::vec4 colors[200];			// w is intensity
		alignas(16) uint32_t numLights;
	};

	struct VkUploadMatrices {
		alignas(16) glm::mat4 viewMatrix{};
		alignas(16) glm::mat4 projectionMatrix{};
	};

	struct VkPushConstants {
		uint32_t pkModelStride;
		uint32_t pkWorldPosOffset;
		uint32_t pkSkinMatOffset;
		uint32_t pkBasePickId;
		uint32_t pkPickId;
	};

	struct VkComputePushConstants {
		uint32_t pkModelOffset;
	};

	enum class instanceEditMode : uint8_t {
		move = 0,
		rotate,
		scale
	};

	//struct secondaryRenderpassInfo {
	//	colorAttachment color;
	//};

	//struct colorAttachment {
	//	VkAttachmentLoadOp loadOp;
	//	VkAttachmentStoreOp storeOp;
	//	VkAttachmentLoadOp stencilLoadOp;
	//	VkAttachmentStoreOp	stencilStoreOp;
	//	VkImageLayout initialLayout;
	//};


	// For shared usage (editor) - Add more if/when necessary - Warning: DO NOT cause these data members to reallocate. We're not detecting/protecting against that
	struct MatrixBuffersView {
		Span<VkUniformBufferData>       viewProjUBOs;
		Span<VkShaderStorageBufferData> modelRootSSBOs;
		Span<VkShaderStorageBufferData> boneMatSSBOs;
	};

	struct LightingBuffersView {
		Span<VkUniformBufferData>       viewPointLightUBOs;
	};

	struct VkRenderData {

		/**
		* Here you'll find many of the resources required to operate the vulkan api.
		* However, the EngineDevice still maintains these responsibilities: 
			- command buffer operations
			- queues
			- command pools
		*/

		// Tmp
		int camera = 1; // which camera we're using
		AvengCameraProxy cameraProxy{
			glm::mat4(1.f),
			glm::mat4(1.f)
		}; // Matrices from the camera that's in use

		int rdWidth = 0;
		int rdHeight = 0;

		uint32_t selectedPickId = 0;

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
		std::vector<VkCommandBuffer> rdCommandBuffersGraphics;	// x
		std::vector<VkCommandBuffer> rdCommandBuffersCompute;	// x
		std::vector<VkCommandBuffer> rdLineCommandBuffers;		// x Editor and Renderer
		std::vector<VkCommandBuffer> rdGUICommandBuffers;		// x Editor and Renderer

		VkRenderPass rdLineRenderpass = VK_NULL_HANDLE;			// x			// EDITOR
		VkRenderPass rdSelectionRenderpass = VK_NULL_HANDLE;	// x						// EDITOR
		VkRenderPass rdImguiRenderpass = VK_NULL_HANDLE;		// x					// EDITOR

		/**
		* Sync
		*/
		std::vector<VkSemaphore> rdPresentSemaphore;
		std::vector<VkSemaphore> rdRenderSemaphore;
		std::vector<VkSemaphore> rdGraphicSemaphore;
		std::vector<VkSemaphore> rdComputeSemaphore;
		std::vector<VkFence> rdRenderFence;
		std::vector<VkFence> rdComputeFence;

		/*
		* Descriptors
		*/
		VkDescriptorPool avengDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorPool editorDescriptorPool = VK_NULL_HANDLE;

		VkDescriptorSetLayout rdAvengDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengAnimationDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengTextureDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengComputeTransformDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengComputeMatrixMultDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengComputeMatrixMultPerModelDescriptorLayout = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdAvengSelectionDescriptorLayout = VK_NULL_HANDLE; // EDITOR
		VkDescriptorSetLayout rdAvengAnimationSelectionDescriptorLayout = VK_NULL_HANDLE; // EDITOR
		VkDescriptorSetLayout rdLineDescriptorLayout = VK_NULL_HANDLE; // EDITOR
		VkDescriptorSetLayout rdPointLightDescriptorLayout = VK_NULL_HANDLE; // EDITOR

		std::vector<VkDescriptorSet> rdAvengDescriptorSets;
		std::vector<VkDescriptorSet> rdAvengAnimationDescriptorSets;
		std::vector<VkDescriptorSet> rdAvengComputeTransformDescriptorSets;
		std::vector<VkDescriptorSet> rdAvengComputeMatrixMultDescriptorSets;
		// std::vector<VkDescriptorSet> basicLightingDescriptorSets;
		std::vector<VkDescriptorSet> textureDescriptorSets;
		std::vector<VkDescriptorSet> rdAvengSelectionDescriptorSets;	// EDITOR
		std::vector<VkDescriptorSet> rdAvengAnimationSelectionDescriptorSets; // EDITOR
		std::vector<VkDescriptorSet> rdLineDescriptorSets;			// EDITOR

		/*
		* Pipeline
		*/
		VkPipelineLayout rdAvengPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengAnimationPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengSelectionPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengAnimationSelectionPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdLinePipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengComputeTransformPipelineLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengComputeMatrixMultPipelineLayout = VK_NULL_HANDLE;

		VkPipeline rdAvengPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengAnimationPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengSelectionPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengAnimationSelectionPipeline = VK_NULL_HANDLE;
		VkPipeline rdLinePipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengComputeTransformPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengComputeMatrixMultPipeline = VK_NULL_HANDLE;

		//VkDescriptorPool rdImguiDescriptorPool = VK_NULL_HANDLE;
		std::vector<VkShaderStorageBufferData> rdSelectedInstanceBuffers; // Storage Buffer

	/*
	* Editor Data
	*/
	instanceEditMode rdInstanceEditMode = instanceEditMode::move;

	MatrixBuffersView matrixBuffersView; // Proxy for editor
	LightingBuffersView pointLightBufferView;

	std::vector<VkImage> rdSelectionImages;
	std::vector<VkImageView> rdSelectionImageViews;
	VkFormat rdSelectionFormat = VK_FORMAT_UNDEFINED;
	std::vector<VmaAllocation> rdSelectionImageAllocs;
	};
}