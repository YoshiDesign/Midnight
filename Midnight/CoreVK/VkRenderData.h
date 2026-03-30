#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <atomic>

#include "Utils/glm_includes.h"

#include <vulkan/vulkan.h>
#include "AMD/vk_mem_alloc.h"

#include <assimp/material.h>

namespace aveng {

	enum class MapMode { OnDemand, Persistent, GpuOnly };
	enum class ResidentMode { CPU, GPU };

	const size_t BINDLESS_TEXTURE_BINDING_0 = 0;

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
		glm::vec4 normal = glm::vec4(0.0f);		// last float is uv.y
		glm::vec4 color = glm::vec4(1.0f);
		glm::uvec4 boneNumber = glm::uvec4(0);
		glm::vec4 boneWeight = glm::vec4(0.0f);
	};

	struct VkBasicVertex {
		glm::vec4 position = glm::vec4(0.0f);	// last float is uv.x
		glm::vec4 normal = glm::vec4(0.0f);		// last float is uv.y
		glm::vec4 color = glm::vec4(1.0f);
	};

	struct VkMesh {
		std::vector<VkVertex> vertices{};
		std::vector<uint32_t> indices{};
		std::unordered_map<aiTextureType, std::string> textures{}; // TODO: No longer necessary with bindless
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

	// Uniform scalars, ezpz
	struct alignas(16) ModelSkinMeta {
		uint32_t boneOffsetBase;
		uint32_t boneParentBase;
		uint32_t boneCount;
		uint32_t pad;
	};

	// UBO - Binding 9 Basic Terrain Pipeline
	struct alignas(16) BasicTerrainAlignmentData {

		// baseCore + countCore = haloBase
		uint32_t baseCorePosition;
		uint32_t countCorePosition;
		uint32_t countHaloPosition;

		uint32_t baseCoreTriangle;
		uint32_t countCoreTriangle;
		uint32_t countHaloTriangle;

		uint32_t baseCoreAdjacency;
		uint32_t countCoreAdjacency;
		uint32_t countHaloAdjacency;

		// Used to distinguish output ownership from input availability
		glm::vec2 coreMinXZ;
		glm::vec2 coreMaxXZ;
		glm::vec2 supportMinXZ;
		glm::vec2 supportMaxXZ;
	};

	struct VkPushConstants {
		uint32_t pkModelBoneStride;
		uint32_t pkInstanceBaseIndex;
		uint32_t pkSkinMatOffset;
		uint32_t pkPickId;
	};

	struct VkComputePushConstants {
		uint32_t pkModelOffset;
		uint32_t skinMetaIndex;
	};

	struct VkBasicTerrainPushConstant {
		uint32_t baseVertex;
		uint32_t baseTriangle;
		uint32_t numVertices;
		uint32_t numTriangles;
	};

	struct VkBasicTerrainDebugPC {
		glm::mat4 modelMat;
	};

	//struct PK {
	//	uint32_t baseVertex;
	//	uint32_t baseTriangle;
	//	uint32_t numVertices;
	//	uint32_t numTriangles;
	//};

	struct VkBasicTerrainComputePushConstants {
		uint32_t baseVertex;
		uint32_t baseTriangle;
		uint32_t numVertices;
		uint32_t numTriangles;
	};

	// std140-compatible settings for the terrain compute shader (binding 0)
	struct alignas(16) TerrainComputeSettings {
		uint32_t normalMode;        // 0 = area-weighted, 1 = angle-weighted
		uint32_t weightOutputMode;  // 0 = SSBO, 1 = image, 2 = both
		uint32_t numLayers;
		uint32_t weightmapWidth;
		glm::vec4 upDir;
		float snowStart;
		float snowEnd;
		float rockStart;
		float rockEnd;
		float eps;
		float _pad0;
		float _pad1;
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
	// TODO: Rename this, it's for more than mat's
	struct MatrixBuffersView {
		Span<VkUniformBufferData>       viewProjUBOs;
		Span<VkShaderStorageBufferData> modelRootSSBOs;
		Span<VkShaderStorageBufferData> boneMatSSBOs;
		Span<VkShaderStorageBufferData> materialSSBOs;
		Span<VkShaderStorageBufferData> materialExtSSBOs;
	};

	struct LightingBuffersView {
		Span<VkUniformBufferData>       viewPointLightUBOs;
	};

	struct GlobalSkeletonBufferState {
		uint32_t nextBoneOffsetMatIdx = 0; // index into bone offset matrices
		uint32_t nextBoneParentIdxIdx = 0; // index of a bone's node's parent-node index
	};

	struct DefaultSamplers {
		VkSampler linearRepeat = VK_NULL_HANDLE;
		VkSampler linearClamp = VK_NULL_HANDLE;
		VkSampler nearestRepeat = VK_NULL_HANDLE;
	};

	struct StorageImageResource {
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFormat format = VK_FORMAT_UNDEFINED;
		uint32_t width = 0;
		uint32_t height = 0;
	};

	struct TerrainStats {
		std::atomic<int> haloPoints = 0;
		std::atomic<int> corePoints = 0;
		std::atomic<int> superPoints = 0;
		std::atomic<int> total = 0;
	};

	struct VkRenderData {

		/**
		* Here you'll find many of the resources required to operate the vulkan api.
		* However, the EngineDevice still maintains these responsibilities: 
			- command buffer operations
			- queues
			- command pools
		*/

		int MAX_FRAMES_IN_FLIGHT = 0;

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

		float rdDrawTime = 0.f;
		float rdFrameTime = 0.f; // AvengFrame.h
		float rdComputeTime = 0.f;
		float rdMatrixGenerateTime = 0.f;
		float rdUploadSSBO1Time = 0.f;
		float rdUploadSSBO2Time = 0.f;
		float rdUploadToUBOTime = 0.f;
		float rdUIGenerateTime = 0.f;
		float rdUIDrawTime = 0.f;
		float rdFramePacketTime = 0.f;

		// Terrain Control Timings
		float rdTerrainBufferTimeInit;	// Vulkan GPU Upload process 
		float rdTerrainBufferTimeInitMAX = 0.f;	
		float rdTerrainCleanupOneTime;	// Vulkan GPU Cleanup process 
		float rdTerrainCleanupOneTimeMAX = 0.f;
		float rdTerrainCopyBufferTime;			// Vulkan GPU Cleanup process 
		float rdTerrainCopyBufferTimeMAX = 0.f;
		float rdTerrainVboIboTime = 0.f;		// CPU-side VBO/IBO creation time
		float rdTerrainVboIboTimeMAX = 0.f;
		float rdTerrainSsbo1Time = 0.f;			// Compute SSBO Input
		float rdTerrainSsbo1TimeMAX = 0.f;
		float rdTerrainSsbo2Time = 0.f;			// Compute SSBO Output
		float rdTerrainSsbo2TimeMAX = 0.f;
		float rdTerrainDescriptor1WriterTime = 0.f;	// Terrain Descriptor Set writer
		float rdTerrainDescriptor1WriterTimeMAX = 0.f;
		float rdTerrainDescriptor2WriterTime = 0.f;	// Terrain Descriptor Set writer
		float rdTerrainDescriptor2WriterTimeMAX = 0.f;
		float rdqueueSubmitTimer = 0.f;
		float rdqueueSubmitTimerMAX = 0.f;
		float rdTerrainRetireTime = 0.f;
		float rdTerrainRetireTimeMAX = 0.f;
		float rdTerrainDrainTime = 0.f;
		float rdTerrainDrainTimeMAX = 0.f;
		float rdTerrainCleanupDeferredDeletesTime = 0.f;
		float rdTerrainCleanupDeferredDeletesTimeMAX = 0.f;

		// Frame-level fence wait timing (diagnostic)
		float rdFenceWaitTime = 0.f;
		float rdFenceWaitTimeMAX = 0.f;
		float rdAcquireTime = 0.f;
		float rdAcquireTimeMAX = 0.f;
		float rdImageFenceWaitTime = 0.f;
		float rdImageFenceWaitTimeMAX = 0.f;

		int rdMoveForward = 0;
		int rdMoveRight = 0;
		int rdMoveUp = 0;

		float rdViewAzimuth = 330.0f;
		float rdViewElevation = -20.0f;
		glm::vec3 rdCameraWorldPosition = glm::vec3(2.0f, 5.0f, 7.0f);

		/* "Allocator-like" Animation Data */
		GlobalSkeletonBufferState skinState{0,0};
		TerrainStats terrStats{0,0,0,0};


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
		
		DefaultSamplers default_samplers;

		std::vector<StorageImageResource> pgStorageImage{};

		/**
		* Sync
		*/
		std::vector<VkSemaphore> rdPresentSemaphore;
		std::vector<VkSemaphore> rdRenderSemaphore;
		std::vector<VkSemaphore> rdGraphicSemaphore;
		std::vector<VkSemaphore> rdComputeSemaphore; // Used by Animation compute (there are 2) and terrain compute (1)
		std::vector<VkFence> rdRenderFence;
		std::vector<VkFence> rdComputeFence; // Used by Animation compute (there are 2) and terrain compute (1)
		//std::vector<VkFence> rdRuntimeGraphicsFence;
		//std::vector<VkFence> rdRuntimeComputeFence;
		std::vector<VkFence> rdImagesInFlight; // Tracks which frame's fence is using each swapchain image

		/* New Bindless Resources */
		VkDescriptorPool avengBindlessDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdBindlessDescriptorLayout = VK_NULL_HANDLE;		
		VkPipelineLayout rdAvengBindlessPipelineLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> rdAvengBindlessDescriptorSets;		

		/* Terrain Resources - Minimal Sets */
		VkDescriptorPool avengBasicTerrainDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdBasicTerrainDescriptorSetLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdAvengBasicTerrainPipelineLayout = VK_NULL_HANDLE;
		std::vector<VkDescriptorSet> rdAvengBasicTerrainDescriptorSets;

		/* Terrain Resources - Lit */
		VkDescriptorPool avengTerrainLitDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdTerrainLitGraphicsDescriptorSetLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdTerrainLitPipelineLayout = VK_NULL_HANDLE;

		/* Terrain Resources - Compute */
		VkDescriptorPool avengTerrainComputeDescriptorPool = VK_NULL_HANDLE;
		VkDescriptorSetLayout rdTerrainComputeDescriptorSetLayout = VK_NULL_HANDLE;
		VkPipelineLayout rdTerrainComputePipelineLayout = VK_NULL_HANDLE;

		VkPipeline rdDebugPipeline = VK_NULL_HANDLE;
		VkPipeline rdDebugAnimatedPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengAnimationPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengSelectionPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengAnimationSelectionPipeline = VK_NULL_HANDLE;
		VkPipeline rdLinePipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengComputeTransformPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengComputeMatrixMultPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengBasicTerrainPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengEditorBasicTerrainPipeline = VK_NULL_HANDLE;
		VkPipeline rdTerrainComputePipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengTerrainLitPipeline = VK_NULL_HANDLE;
		VkPipeline rdAvengEditorTerrainLitPipeline = VK_NULL_HANDLE;

		std::vector<VkShaderStorageBufferData> rdShaderBoneMatrixOffsetBuffers;	// Storage Buffer
		std::vector<glm::mat4> globalBoneOffsetMatricesList{};					// Data - Reserved/Resized during model loading

		std::vector<VkShaderStorageBufferData> rdBoneParentNodeIndexBuffers;		// Storage Buffer
		std::vector<int32_t> globalBoneParentIndexList{}; // Data - Reserved/Resized during model loading

		std::vector<VkUniformBufferData> rdBoneMetaBuffers;	// Uniform Buffer
		std::vector<ModelSkinMeta> rdBoneMetaBufferData{};	// Data - Note: this buffer is either appended to or cleared. Never updated arbitrarily

		std::vector<VkShaderStorageBufferData> rdInstanceMaterialBuffers;
		std::vector<VkShaderStorageBufferData> rdInstanceMaterialExtBuffers;

		/*
		 * Editor Data
		 */
		instanceEditMode rdInstanceEditMode = instanceEditMode::move;

		MatrixBuffersView matrixBuffersView; // Proxy for editor. TODO - Rename it
		LightingBuffersView pointLightBufferView;

		std::vector<VkImage> rdSelectionImages;
		std::vector<VkImageView> rdSelectionImageViews;
		VkFormat rdSelectionFormat = VK_FORMAT_UNDEFINED;
		std::vector<VmaAllocation> rdSelectionImageAllocs;

	};
}