#pragma once

#include <memory>
#include <vector>
#include <unordered_map>

#include "Core/PointLightSystem.h"
//#include "Core/aveng_scene_loader.h"

#include "CoreVK/swapchain.h"
#include "CoreVK/VkRenderData.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Utils/Timer.h"
#include "Utils/glm_includes.h"

namespace aveng {

	class EngineDevice;
	class AssimpInstance;
	class AvengModel;
	class AvengWindow;
	class CameraManager;
	struct CameraTransform;

	class Renderer {

		void updateTriangleCount();

	public:

		Renderer(EngineDevice& engineDevice, AvengWindow& window, VkRenderData& renderData, ModelAndInstanceData& mModelInstanceData, CameraManager& cameraManager);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		void initialize();

		// Our app needs to be able to access the swap chain render pass in order to configure any pipelines it creates
		SwapChain* pGetSwapChain() const { return aveng_swapchain.get(); }
		uint32_t* pGetCurrentImageIndex() { return &currentImageIndex; }
		VkImage& getImage(int index) { return aveng_swapchain->getImage(index); }
		float getAspectRatio() const { return aveng_swapchain->extentAspectRatio(); }
		float getPixelValueFromPos(unsigned int mMouseXPos, unsigned int mMouseYPos) { return aveng_swapchain->getPixelValueFromPos(mMouseXPos, mMouseYPos, currentImageIndex); };
		uint32_t getImageCount() const { return aveng_swapchain->imageCount(); }

		VkRenderPass getSwapChainRenderPass() const { return aveng_swapchain->getRenderPass(); }
		VkRenderPass getSelectionRenderPass() const { return renderData.rdSelectionRenderpass; }
		VkRenderPass getLineRenderPass() const { return renderData.rdLineRenderpass; }

		const PointLightData getPointLightData() { return mPointLightData; }

		VkSwapchainKHR getVkSwapchain() const { return aveng_swapchain->getSwapchain(); }
		VkFormat getSwapChainImageFormat() { return aveng_swapchain->getSwapChainImageFormat(); }
		VkFramebuffer getCurrentFramebuffer() { return aveng_swapchain->getFrameBuffer(currentImageIndex); }
		VkFramebuffer getCurrentSelectionFramebuffer() { return aveng_swapchain->getSelectionFrameBuffer(currentImageIndex); }

		bool isFrameInProgress() const { return isFrameStarted; }
		bool createPipelineLayouts();
		bool createPipelines();

		// Add public method:
		bool queueModelLoad(const std::string& filepath);
		void processPendingModelLoads();  // Call this before/after frames

		// These functions might get moved to ObjectRenderSystem
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
		bool createLightsUBO();

		void updateLights();

		// Just use the returned values directly if working in renderer.cpp. This is for clients
		VkCommandBuffer getCurrentCommandBufferGraphics() const 
		{
			assert(isFrameStarted && "Cannot get command buffer. The frame is not in progress.");
			return renderData.rdCommandBuffersGraphics.at(currentFrameIndex);
		}

		// Just use the returned values directly if working in renderer.cpp. This is for clients
		VkCommandBuffer getCurrentCommandBufferCompute() const
		{
			assert(isFrameStarted && "Cannot get command buffer. The frame is not in progress.");
			return renderData.rdCommandBuffersCompute.at(currentFrameIndex);
		}

		int getFrameIndex() const
		{
			// Note: This assertion saved you from serious headache. Assertions are great, use them
			assert(isFrameStarted && "Cannot get the frame index when frame is not in progress.");
			return currentFrameIndex;
		}

		void setRenderpassBypass(bool _state) { mRenderpassBypass = _state; }

		bool createDescriptorLayouts();
		bool createDescriptorSets();
		bool setupDescriptors();
		void updateDescriptorSets(int frameIndex);
		void updateComputeDescriptorSets(int frameIndex);
		void updateLightingDescriptorSets(int frameIndex);

		bool createSyncObjects();
		
		// Descriptor set accessors
		//VkDescriptorSet getGlobalDescriptorSet(int frameIndex) const { return globalDescriptorSets[frameIndex]; }
		//VkDescriptorSet getLightsDescriptorSet(int frameIndex) const { return lightsDescriptorSets[frameIndex]; }

		int draw(float deltaTime);
		bool drawModels(
			VkCommandBuffer commandBuffer, 
			VkPipeline basicPipeline, 
			VkPipeline animationPipeline, 
			VkPipelineLayout basicLayout, 
			VkPipelineLayout animationLayout, 
			VkDescriptorSet basicDescriptorSet,
			VkDescriptorSet animationDescriptorSet,
			int frameIndex);

		bool beginFrame(); // Synchronization
		void endFrame(); // Synchronization
		void beginSwapChainRenderPass(VkCommandBuffer commandBuffer, VkFramebuffer frameBuffer, VkRenderPass renderpass, bool selection = false);
		void endSwapChainRenderPass(VkCommandBuffer commandBuffer);

		// New methods for descriptor/buffer management
		void updateCamera();

		void runComputeShaders(std::shared_ptr<AvengModel> model, int numInstances, uint32_t modelOffset);
		
		void initializePointLights();
		void renderLights(const VkPipeline& pipeline, const VkPipelineLayout& layout);
		int getLightCount() const { return mPointLightData.numLights; }
		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius);
		void clearLights();

		// void loadScenes(const char* filepath);
		const CameraTransform& activeCameraTransform();

		// Newest methods:
		void beginGraphicsCommands(int currentFrameIndex);
		void endGraphicsCommands(int currentFrameIndex);
		void recreateSwapChain();
		bool isRecreatingSwapChain() { return recreatingSwapchain; }
		
		void destroyTrash();
		void cleanup();

	private:

		VkRenderData& renderData;
		size_t boneMatrixBufferSize;

		bool firstFrame = true;
		bool mRenderpassBypass = false;
		bool recreatingSwapchain = false;

		// Engine systems
		AvengWindow& aveng_window;
		EngineDevice& engineDevice;
		CameraManager& cameraManager;

		VkResult err;
		VkResult result;

		Timer mFrameTimer{};
		Timer mMatrixGenerateTimer{};
		Timer mUploadToUBOTimer{};
		Timer mUIGenerateTimer{};
		Timer mUIDrawTimer{};

		// const char* default_scene_file = "scenes/demo-scene.json";

		//AvengSceneLoader sceneLoader{ renderData };			// Contains shared pointers to objects with VMA Buffer Allocation
		std::unique_ptr<SwapChain> aveng_swapchain;			// Swapchain - Heap Allocated makes it easier to rebuild when the window resizes
		PointLightSystem pointLightSystem{ engineDevice, renderData };	// Light stuff
		
		// Dynamic texture array support
		uint32_t currentImageIndex{ 0 };
		int currentFrameIndex{ 0 }; // Not tied to the image index
		bool isFrameStarted{ false };

		void createCommandBuffers();
		void freeCommandBuffers();
		
		size_t calculateDynamicUBOStride() const;

		// Descriptors and Buffers
		std::vector<VkUniformBufferData> mPerspectiveViewMatrixUBOBuffers;
		std::vector<VkUniformBufferData> mPointLightUBOBuffers;
		std::vector<VkShaderStorageBufferData> mShaderModelRootMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mShaderBoneMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mShaderTrsMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mNodeTransformBuffers;

		std::vector<PendingBufferDestroy> buffer_trash;

		// Renderer owns this
		ModelAndInstanceData& mModelInstanceData; 

		VkUploadMatrices mMatrices{ glm::mat4(1.0f), glm::mat4(1.0f) };
		VkPushConstants mModelData{};
		VkComputePushConstants mComputeModelData{};

		/* for animated and non-animated models */

		std::vector<glm::mat4> mWorldPosMatrices{};
		std::vector<NodeTransformData> mNodeTransFormData{};
		PointLightData mPointLightData{};

	};

} 