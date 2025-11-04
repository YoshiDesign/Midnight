#pragma once

#include <memory>
#include <vector>
#include <cassert>
#include <unordered_map>
#include "CoreVK/aveng_descriptors.h"
#include "CoreVK/AvengImageSystem.h"
#include "CoreVK/PointLightSystem.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/aveng_buffer.h"
#include "CoreVK/GFXPipeline.h"
#include "CoreVK/swapchain.h"
#include "CoreVK/CommandBuffers.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/Renderer/PipelineConfigManager.h"
#include "Core/Modeling/AssimpInstance.h"
#include "Core/aveng_scene_loader.h"
#include "Core/aveng_window.h"
#include "Core/aveng_model.h"
#include "Core/app_object.h"
#include "Core/data.h"
#include "Utils/Timer.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "CoreVK/AvengUniformBuffer.h"

#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "CoreVK/VkRenderData.h"

namespace aveng {

	class Renderer {

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

		VkCommandBuffer getCurrentCommandBufferGraphics() const 
		{
			assert(isFrameStarted && "Cannot get command buffer. The frame is not in progress.");
			return renderData.rdCommandBuffersGraphics[currentFrameIndex];
		}

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

		void setupDescriptors();
		void updateDescriptorSets();
		void updateComputeDescriptorSets();
		void updateLightingDescriptorSets();
		
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
		void beginSwapChainRenderPass(VkCommandBuffer commandBuffer, glm::vec3 rgb);
		void endSwapChainRenderPass(VkCommandBuffer commandBuffer);

		// New methods for descriptor/buffer management
		void initializeImageSystem(const std::vector<std::string>& texturePaths);
		void initializePointLightSystem();
		void updateFrameData(const glm::mat4& projection, const glm::mat4& view);

		void renderEditor();

		void runComputeShaders(std::shared_ptr<AvengModel> model, int numInstances, uint32_t modelOffset);
		
		const std::vector<AvengAppObject>& getAppObjects() const { return sceneLoader.getAppObjects(); };

		void renderLights();
		int getLightCount() const { return u_LightsData.numLights; }
		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius);
		void clearLights();

		void loadScenes(const char* filepath);
		
		// Pipeline management methods
		bool reloadPipelineConfig(const std::string& configPath = "");
		std::vector<std::string> getAvailablePipelines() const;

	private:
		VkResult err;
		GameData& gameData;
		VkRenderData renderData;
		VkPushConstants mModelData{};
		ModelAndInstanceData mModelInstanceData{};
		VkComputePushConstants mComputeModelData{};
		VkUniformBufferData mPerspectiveViewMatrixUBO{};

		Timer mFrameTimer{};
		Timer mMatrixGenerateTimer{};
		Timer mUploadToUBOTimer{};
		Timer mUIGenerateTimer{};
		Timer mUIDrawTimer{};

		// Uniform buffer V1
		LightsUbo u_LightsData{};
		VkUploadMatrices mMatrices{ glm::mat4(1.0f), glm::mat4(1.0f) };

		// Shader Data
		std::vector<glm::mat4> mWorldPosMatrices{};	
		std::vector<NodeTransformData> mNodeTransFormData{};
		VkShaderStorageBufferData mShaderModelRootMatrixBuffer{}; // For animated and non-animated models
		VkShaderStorageBufferData mShaderBoneMatrixBuffer{}; // For animated models
		VkShaderStorageBufferData mShaderTRSMatrixBuffer{};
		VkShaderStorageBufferData mShaderNodeTransformBuffer{};

		const char* default_scene_file = "scenes/demo-scene.json";

		// Engine systems
		AvengWindow& aveng_window;
		EngineDevice engineDevice{ aveng_window };			// The Engine Service - Stack allocated
		AvengSceneLoader sceneLoader{ renderData };			// Contains shared pointers to objects with VMA Buffer Allocation
		std::unique_ptr<SwapChain> aveng_swapchain;			// Swapchain - Heap Allocated makes it easier to rebuild when the window resizes
		std::unique_ptr<ImageSystem> imageSystem;			// Texture stuff
		PointLightSystem pointLightSystem{ engineDevice };	// Light stuff
		
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
		std::unique_ptr<PipelineConfigManager> pipelineManager = nullptr;
		VkPipelineLayout pipelineLayout{};
		void createPipelineLayout();
		void createPipelines();
		bool pipelineCreated = false;
		
		// Descriptors and Buffers
		std::unique_ptr<AvengDescriptorPool> descriptorPool{};
		std::vector<std::unique_ptr<AvengBuffer>> mPerspectiveViewMatrixUBOBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> mShaderModelRootMatrixBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> mShaderBoneMatrixBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> mNodeTransformBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> mTrsMatrixBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> mLightDataBuffers;

#ifdef ENABLE_EDITOR
		aveng::Editor editor{ renderData, gameData, engineDevice };
#endif

	};

}