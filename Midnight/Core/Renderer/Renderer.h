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
#include "Core/Renderer/PipelineConfigManager.h"
#include "Core/Animation/AnimationManager.h"
#include "Core/aveng_scene_loader.h"
#include "Core/aveng_window.h"
#include "Core/aveng_model.h"
#include "Core/app_object.h"
#include "Core/data.h"


#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace aveng {

	

	// Batch data for grouping instances by model
	//struct RenderBatch {
	//	AvengModel* model;
	//	std::vector<InstanceData> instances;
	//	std::unique_ptr<AvengBuffer> instanceBuffer;
	//	
	//	RenderBatch(AvengModel* m) : model(m) {}
	//};

	class Renderer {

	public:

		Renderer(AvengWindow& window, GameData& _gameData);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer &operator=(const Renderer&) = delete;

		RenderData renderData;
		GameData& gameData;

		// Our app needs to be able to access the swap chain render pass in order to configure any pipelines it creates
		VkRenderPass getSwapChainRenderPass() const { return aveng_swapchain->getRenderPass(); }
		float getAspectRatio() const { return aveng_swapchain->extentAspectRatio(); }
		bool isFrameInProgress() const { return isFrameStarted; }

		VkCommandBuffer getCurrentCommandBuffer() const 
		{
			assert(isFrameStarted && "Cannot get command buffer. The frame is not in progress.");
			return commandBuffers[currentFrameIndex];
		}

		int getFrameIndex() const
		{
			assert(isFrameStarted && "Cannot get the frame index when frame is not in progress.");
			return currentFrameIndex;
		}
		
		// Descriptor set accessors
		VkDescriptorSet getGlobalDescriptorSet(int frameIndex) const { return globalDescriptorSets[frameIndex]; }
		VkDescriptorSet getLightsDescriptorSet(int frameIndex) const { return lightsDescriptorSets[frameIndex]; }
		VkPipelineLayout getPipelineLayout() const { return pipelineLayout; }

		// SwapChain getters
		uint32_t getImageCount() const { return aveng_swapchain->imageCount(); }
		VkImage& getImage(int index) { return aveng_swapchain->getImage(index); }
		VkFormat getSwapChainImageFormat() { return aveng_swapchain->getSwapChainImageFormat(); }

		void beginFrame();
		void endFrame();
		void beginSwapChainRenderPass(VkCommandBuffer commandBuffer, glm::vec3 rgb);
		void endSwapChainRenderPass(VkCommandBuffer commandBuffer);

		VkDevice getEngineDevice() { return engineDevice.device(); }

		// New methods for descriptor/buffer management
		void initializeImageSystem(const std::vector<std::string>& texturePaths);
		void setupDescriptors();
		void initializePointLightSystem();
		void updateFrameData(const glm::mat4& projection, const glm::mat4& view);

		void renderEditor();
		
		// Animation system integration (delegated to AnimationRenderingSystem)
		void updateAnimationData(const std::vector<std::shared_ptr<class AssimpInstance>>& instances, float deltaTime = 0.016f);
		void dispatchAnimationCompute(uint32_t vertexCount);
		void renderAnimatedModels(const std::vector<std::shared_ptr<class AssimpInstance>>& instances);
		void renderObjects(const std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>>& objectData);
		void renderObjectsInstanced(const std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>>& objectData);
		// void updateInstanceBuffer(RenderBatch& batch);
		const std::vector<AvengAppObject>& getAppObjects() const { return sceneLoader.getAppObjects(); };
		void renderLights();
		int getLightCount() const { return u_LightsData.numLights; }

		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius);
		void clearLights();

		// Instanced rendering controls
		void setInstancedRenderingEnabled(bool enabled) { instancedRenderingEnabled = enabled; }
		bool getInstancedRenderingEnabled() const { return instancedRenderingEnabled; }
		//uint32_t getBatchCount() const { return static_cast<uint32_t>(renderBatches.size()); }

		// Pipeline switching methods
		void setObjectRenderMode(ObjectRenderMode mode) { currentObjectMode = mode; }
		void setPostProcessMode(PostProcessMode mode) { currentPostProcessMode = mode; }
		ObjectRenderMode getObjectRenderMode() const { return currentObjectMode; }
		PostProcessMode getPostProcessMode() const { return currentPostProcessMode; }

		void loadScenes(const char* filepath);
		
		// Pipeline management methods
		bool reloadPipelineConfig(const std::string& configPath = "");
		std::vector<std::string> getAvailablePipelines() const;

	private:

		const char* default_scene_file = "scenes/demo-scene.json";
		AvengWindow& aveng_window;
		EngineDevice engineDevice{ aveng_window };		// The window API - Stack allocated
		AvengSceneLoader sceneLoader;					// Contains shared pointers to objects with VMA Buffer Allocation
		
		// Dynamic texture array support
		uint32_t currentTextureCount = 8;	// Track current texture count for pipeline creation
		bool pipelineCreated = false;		// Guard against double pipeline creation

		// Uniform buffers
		LightsUbo u_LightsData{};
		GlobalUbo u_GlobalData{};

		// Pipeline mode tracking
		ObjectRenderMode currentObjectMode = ObjectRenderMode::STANDARD;
		PostProcessMode currentPostProcessMode = PostProcessMode::NONE;

		VkResult err;

		void createCommandBuffers();
		void freeCommandBuffers();
		void recreateSwapChain();

		// Moved from ObjectRenderSystem
		void createPipelineLayout();
		void createPipelines();                 // Main pipeline creation entry point
		//void createPostProcessPipelines();
		
		// DEPRECATED: Legacy pipeline creation (to be removed)
		void createPipeline();
		
		size_t calculateDynamicUBOStride() const;

		// Animation
		/*void updateAnimationSystem(float frameTime);
		void testAnimationSystem();*/

		VkCommandBuffer commandBuffer;
		std::vector<VkCommandBuffer> commandBuffers;

		// SwapChain aveng_swapchain{ engineDevice, aveng_window.getExtent() };	// previous stack allocated. Ptr makes it easier to rebuild when the window resizes
		std::unique_ptr<SwapChain> aveng_swapchain;
		
		uint32_t currentImageIndex{0};
		int currentFrameIndex; // Not tied to the image index
		bool isFrameStarted{ false };

		// Pipeline management
		std::unique_ptr<PipelineConfigManager> pipelineManager;
		std::vector<std::unique_ptr<GFXPipeline>> objectPipelines;  // DEPRECATED: Fallback only

		// Animation system for testing
		//AnimationManager animationManager;

		// Animation rendering system
		// std::unique_ptr<class AnimationRenderingSystem> animationSystem = nullptr;
		
		// DEPRECATED: Remove these legacy pipelines - use PipelineConfigManager instead
		std::unique_ptr<GFXPipeline> gfxPipeline;  // Legacy - to be removed
		std::unique_ptr<GFXPipeline> gfxPipeline2; // Legacy - to be removed
		
		// Post-processing pipelines and resources
		std::vector<std::unique_ptr<GFXPipeline>> postProcessPipelines;  // Index corresponds to PostProcessMode
		//VkRenderPass offscreenRenderPass{};
		//std::vector<VkFramebuffer> offscreenFramebuffers;
		//VkImage offscreenColorImage{};
		//VkDeviceMemory offscreenColorImageMemory{};
		//VkImageView offscreenColorImageView{};
		//VkImage offscreenDepthImage{};
		//VkDeviceMemory offscreenDepthImageMemory{};
		//VkImageView offscreenDepthImageView{};
		
		VkPipelineLayout pipelineLayout{};
		VkPipelineLayout postProcessPipelineLayout{};

		// Descriptor and Buffer management
		std::unique_ptr<AvengDescriptorPool> descriptorPool{};
		std::vector<std::unique_ptr<AvengBuffer>> u_GlobalBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_ObjBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_LightsBuffers;
		std::vector<VkDescriptorSet> globalDescriptorSets;
		std::vector<VkDescriptorSet> objectDescriptorSets;
		std::vector<VkDescriptorSet> lightsDescriptorSets;

		// Post-processing descriptor sets
		std::vector<VkDescriptorSet> postProcessDescriptorSets;

		// Descriptor set layouts (reused by multiple systems)
		std::unique_ptr<AvengDescriptorSetLayout> globalDescriptorSetLayout;
		std::unique_ptr<AvengDescriptorSetLayout> objDescriptorSetLayout;
		std::unique_ptr<AvengDescriptorSetLayout> lightsDescriptorSetLayout;
		std::unique_ptr<AvengDescriptorSetLayout> postProcessDescriptorSetLayout;

		// Instanced rendering data - TODO - Why is a pointer the key? RenderBatch even includes a pointer to AvengModel. wtf?
		// std::unordered_map<AvengModel*, std::unique_ptr<RenderBatch>> renderBatches;
		bool instancedRenderingEnabled = true; // Toggle for A/B testing
		uint32_t maxInstancesPerBatch = 1000;  // Reasonable default

		// Engine systems
		std::unique_ptr<ImageSystem> imageSystem;
		PointLightSystem pointLightSystem{ engineDevice };

		// State
		int num_objects{1};
		
#ifdef ENABLE_EDITOR
		aveng::Editor editor{ renderData, gameData };
#endif

	};

}


/* Note */
// The Graphics API - Previously stack allocated
//GFXPipeline gfxPipeline{
//	engineDevice, 
//	"shaders/simple_shader.vert.spv", 
//	"shaders/simple_shader.frag.spv", 
//	GFXPipeline::defaultPipelineConfig(WIDTH, HEIGHT) 
//};