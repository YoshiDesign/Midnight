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
#include "CoreVK/VkRenderData.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "CoreVK/AvengUniformBuffer.h"
#include "CoreVK/PipelineLayout.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/ComputePipeline.h"
#include "CoreVK/LinePipeline.h"
#include "CoreVK/SyncObjects.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/Modeling/AssimpInstance.h"
#include "Core/aveng_scene_loader.h"
#include "Core/aveng_window.h"
#include "Core/aveng_model.h"
#include "Core/CameraProxy.h"
#include "Core/app_object.h"
#include "Core/data.h"
#include "Utils/Timer.h"
#include "Utils/glm_includes.h"

#ifdef ENABLE_EDITOR
#include "Editor.h"
#include "EditorData.h"
#endif

namespace aveng {

	class Renderer {

		void updateTriangleCount();

	public:

		Renderer(EngineDevice& engineDevice, AvengWindow& window, VkRenderData& renderData, GameData& _gameData, ModelAndInstanceData& mModelInstanceData);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		// Our app needs to be able to access the swap chain render pass in order to configure any pipelines it creates
		VkRenderPass getSwapChainRenderPass() const { return aveng_swapchain->getRenderPass(); }
		float getAspectRatio() const { return aveng_swapchain->extentAspectRatio(); }
		SwapChain* pGetSwapChain() const { return aveng_swapchain.get(); }

		bool isFrameInProgress() const { return isFrameStarted; }
		bool createPipelineLayouts();
		bool createPipelines();

		std::shared_ptr<CameraProxy> getMainCamera();

		// Add public method:
		bool queueModelLoad(const std::string& filepath);
		void processPendingModelLoads();  // Call this before/after frames

		// These 8 functions might get moved to ObjectRenderSystem
		bool hasModel(const std::string& modelFileName);
		std::shared_ptr<AvengModel> getModel(const std::string& modelFileName);
		bool addModel(const std::string& modelFileName);
		void deleteModel(const std::string& modelFileName);
		std::shared_ptr<AssimpInstance> addInstance(std::shared_ptr<AvengModel> model);
		void addInstances(std::shared_ptr<AvengModel> model, int numInstances);
		void deleteInstance(std::shared_ptr<AssimpInstance> instance);
		void cloneInstance(std::shared_ptr<AssimpInstance> instance);
		void cloneInstances(std::shared_ptr<AssimpInstance> instance, int numClones);
		void centerInstance(std::shared_ptr<AssimpInstance> instance);
		void assignInstanceIndices();

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

		int draw(float deltaTime);
		void beginFrame();
		void endFrame();
		void beginSwapChainRenderPass();
		void endSwapChainRenderPass();

		// New methods for descriptor/buffer management
		void initializePointLightSystem();
		void updateFrameData(const glm::mat4& projection, const glm::mat4& view);
#if ENABLE_EDITOR
		void renderEditor();
		void setupEditorFrame(float dt);
#endif
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
		EngineDevice& engineDevice;

		VkResult err;
		VkResult result;
		GameData& gameData;
		VkRenderData& renderData;

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
		uint32_t currentImageIndex{ 0 };
		int currentFrameIndex; // Not tied to the image index
		bool isFrameStarted{ false };

		void createCommandBuffers();
		void freeCommandBuffers();
		void recreateSwapChain();
		size_t calculateDynamicUBOStride() const;

		// Descriptors and Buffers
		std::vector<VkUniformBufferData> mPerspectiveViewMatrixUBOBuffers;
		std::vector<VkShaderStorageBufferData> mShaderModelRootMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mShaderBoneMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mShaderTrsMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mNodeTransformBuffers;
		std::vector<VkShaderStorageBufferData> mLightDataBuffers;

		// Renderer owns this
		ModelAndInstanceData& mModelInstanceData; 

		VkUploadMatrices mMatrices{ glm::mat4(1.0f), glm::mat4(1.0f) };
		VkPushConstants mModelData{};
		VkComputePushConstants mComputeModelData{};

		/* for animated and non-animated models */

		std::vector<glm::mat4> mWorldPosMatrices{};
		std::vector<NodeTransformData> mNodeTransFormData{};

	};

} 