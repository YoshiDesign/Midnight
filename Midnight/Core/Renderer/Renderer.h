#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include "CoreVK/aveng_descriptors.h"
#include "CoreVK/PointLightSystem.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/aveng_buffer.h"
#include "CoreVK/GFXPipeline.h"
#include "CoreVK/swapchain.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/Modeling/AssimpInstance.h"
#include "Core/aveng_scene_loader.h"
#include "Core/aveng_window.h"
#include "Core/aveng_model.h"
#include "Core/app_object.h"
#include "Core/data.h"
#include "Utils/Timer.h"
#include "Utils/glm_includes.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "CoreVK/AvengUniformBuffer.h"
#include "CoreVK/PipelineLayout.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/ComputePipeline.h"
#include "CoreVK/SyncObjects.h"

#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif

#include "Utils/glm_includes.h"

#include "CoreVK/VkRenderData.h"

namespace aveng {

	class Renderer {

		struct PendingModelLoad {
			std::string filepath;
		};
		std::vector<PendingModelLoad> mPendingModelLoads;

		void updateTriangleCount();

	public:

		Renderer(AvengWindow& window, GameData& _gameData);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		// Our app needs to be able to access the swap chain render pass in order to configure any pipelines it creates
		VkRenderPass getSwapChainRenderPass() const { return aveng_swapchain->getRenderPass(); }
		VkDevice getEngineDevice() { return engineDevice.device(); }
		float getAspectRatio() const { return aveng_swapchain->extentAspectRatio(); }
		bool isFrameInProgress() const { return isFrameStarted; }
		bool createPipelineLayouts();
		bool createPipelines();

		// Add public method:
		bool queueModelLoad(const std::string& filepath);
		void processPendingModelLoads();  // Call this before/after frames

		// These 8 functions might get moved to ObjectRenderSystem
		bool hasModel(std::string modelFileName);
		std::shared_ptr<AvengModel> getModel(std::string modelFileName);
		bool addModel(std::string modelFileName);
		void deleteModel(std::string modelFileName);
		std::shared_ptr<AssimpInstance> addInstance(std::shared_ptr<AvengModel> model);
		void addInstances(std::shared_ptr<AvengModel> model, int numInstances);
		void deleteInstance(std::shared_ptr<AssimpInstance> instance);
		void cloneInstance(std::shared_ptr<AssimpInstance> instance);

		bool createSSBOs();
		bool createMatrixUBO();

		// Just use the returned values directly if working in renderer.cpp
		VkCommandBuffer getCurrentCommandBufferGraphics() const 
		{
			assert(isFrameStarted && "Cannot get command buffer. The frame is not in progress.");
			return renderData.rdCommandBuffersGraphics[currentFrameIndex];
		}

		// Just use the returned values directly if working in renderer.cpp
		VkCommandBuffer getCurrentCommandBufferCompute() const
		{
			assert(isFrameStarted && "Cannot get command buffer. The frame is not in progress.");
			return renderData.rdCommandBuffersCompute[currentFrameIndex];
		}

		int getFrameIndex() const
		{
			assert(isFrameStarted && "Cannot get the frame index when frame is not in progress.");
			return currentFrameIndex;
		}

		bool createDescriptorLayouts();
		bool createDescriptorSets();
		bool setupDescriptors();
		void updateDescriptorSets(int iters = 1);
		void updateComputeDescriptorSets(int iters = 1);
		void updateLightingDescriptorSets();

		bool createSyncObjects();
		
		// Descriptor set accessors
		//VkDescriptorSet getGlobalDescriptorSet(int frameIndex) const { return globalDescriptorSets[frameIndex]; }
		//VkDescriptorSet getLightsDescriptorSet(int frameIndex) const { return lightsDescriptorSets[frameIndex]; }

		// SwapChain getters
		uint32_t getImageCount() const { return aveng_swapchain->imageCount(); }
		VkImage& getImage(int index) { return aveng_swapchain->getImage(index); }
		VkFormat getSwapChainImageFormat() { return aveng_swapchain->getSwapChainImageFormat(); }

		bool draw(float deltaTime);
		void beginFrame();
		void endFrame();
		void beginSwapChainRenderPass();
		void endSwapChainRenderPass();

		// New methods for descriptor/buffer management
		void initializePointLightSystem();
		void updateFrameData(const glm::mat4& projection, const glm::mat4& view);

		void renderEditor();

		void runComputeShaders(std::shared_ptr<AvengModel> model, int numInstances, uint32_t modelOffset);
		
		// const std::vector<AvengAppObject>& getAppObjects() const { return sceneLoader.getAppObjects(); };

		void renderLights();
		int getLightCount() const { return u_LightsData.numLights; }
		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius);
		void clearLights();

		// void loadScenes(const char* filepath);
		
		// Pipeline management methods
		bool reloadPipelineConfig(const std::string& configPath = "");
		std::vector<std::string> getAvailablePipelines() const;

		void cleanup();

	private:
		// Engine systems
		AvengWindow& aveng_window;
		EngineDevice engineDevice{ aveng_window };			// The Engine Service - Stack allocated

		VkResult err;
		VkResult result;
		GameData& gameData;
		VkRenderData renderData;

		Timer mFrameTimer{};
		Timer mMatrixGenerateTimer{};
		Timer mUploadToUBOTimer{};
		Timer mUIGenerateTimer{};
		Timer mUIDrawTimer{};

		// Uniform buffer V1
		LightsUbo u_LightsData{};

		// const char* default_scene_file = "scenes/demo-scene.json";

		//AvengSceneLoader sceneLoader{ renderData };			// Contains shared pointers to objects with VMA Buffer Allocation
		std::unique_ptr<SwapChain> aveng_swapchain;			// Swapchain - Heap Allocated makes it easier to rebuild when the window resizes
		PointLightSystem pointLightSystem{ engineDevice, renderData };	// Light stuff
		
		// Dynamic texture array support
		uint32_t currentTextureCount = 8;	// Track current texture count for pipeline creation
		uint32_t currentImageIndex{ 0 };
		int currentFrameIndex; // Not tied to the image index
		bool isFrameStarted{ false };

		// Pipeline mode tracking
		// ObjectRenderMode currentObjectMode = ObjectRenderMode::STANDARD;

		void createCommandBuffers();
		void freeCommandBuffers();
		void recreateSwapChain();
		size_t calculateDynamicUBOStride() const;

		// Main pipeline creation entry point
		//std::unique_ptr<PipelineConfigManager> pipelineManager = nullptr;
		VkPipelineLayout pipelineLayout{};
		void createPipelineLayout();
		bool pipelineCreated = false;

		// Descriptors and Buffers
		std::vector<VkUniformBufferData> mPerspectiveViewMatrixUBOBuffers;
		std::vector<VkShaderStorageBufferData> mShaderModelRootMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mShaderBoneMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mShaderTrsMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mNodeTransformBuffers;
		std::vector<VkShaderStorageBufferData> mLightDataBuffers;

		VkUploadMatrices mMatrices{ glm::mat4(1.0f), glm::mat4(1.0f) };

		ModelAndInstanceData mModelInstanceData{}; 

		// Shader Data
		//std::vector<glm::mat4> mWorldPosMatrices{};
		//std::vector<NodeTransformData> mNodeTransFormData{};
		//std::vector <std::unique_ptr <VkShaderStorageBufferData>> mShaderModelRootMatrixBuffer{}; // For animated and non-animated models
		//std::vector <std::unique_ptr <VkShaderStorageBufferData>> mShaderBoneMatrixBuffer{}; // For animated models
		//std::vector <std::unique_ptr <VkShaderStorageBufferData>> mShaderTRSMatrixBuffer{};
		//std::vector <std::unique_ptr <VkShaderStorageBufferData>> mShaderNodeTransformBuffer{};
		//std::vector <std::unique_ptr <VkUniformBufferData>> mPerspectiveViewMatrixUBO{};

		VkPushConstants mModelData{};
		VkComputePushConstants mComputeModelData{};

		/* for animated and non-animated models */

		std::vector<glm::mat4> mWorldPosMatrices{};

		/* for compute shader */
		bool mHasDedicatedComputeQueue = false;

		std::vector<NodeTransformData> mNodeTransFormData{};


#ifdef ENABLE_EDITOR
		aveng::Editor editor{ renderData, gameData, engineDevice, mModelInstanceData };
#endif

	};

} 