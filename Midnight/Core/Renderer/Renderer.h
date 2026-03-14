#pragma once
#include "avpch.h"
#include "Core/PointLightSystem.h"
// #include "Core/Modeling/ModelAndInstanceData.h"
#include "CoreVK/VkRenderData.h"
#include "Core/aveng_model.h"
#include "CoreVK/swapchain.h"
#include "Utils/Timer.h"
#include "Core/Imaging/TextureRegistry.h"

namespace procgen {
	struct TerrainMeshCpu;
}

namespace aveng {

	struct FramePacket;
	class ModelLibrary;
	class EngineDevice;
	class AssimpInstance;
	class AvengWindow;
	class CameraManager;
	struct CameraTransform;
	struct IRenderSceneView;
	
	class Renderer {

	public:

		Renderer(
			EngineDevice& engineDevice, 
			AvengWindow& window, 
			VkRenderData& renderData, 
			CameraManager& cameraManager,	// These systems could be composed into another "services" struct
			const IModelQuery& mq,			// These systems could be composed into another "services" struct
			IModelAnimQuery& aq				// These systems could be composed into another "services" struct
		);
		~Renderer() = default;

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		void initialize();

		bool isFrameInProgress() const { return isFrameStarted; }

		// Our app needs to be able to access the swap chain render pass in order to configure any pipelines it creates
		SwapChain* pGetSwapChain() const { return aveng_swapchain.get(); }
		uint32_t* pGetCurrentImageIndex() { return &currentImageIndex; }
		VkImage& getImage(int index) { return aveng_swapchain->getImage(index); }
		float getAspectRatio() const { return aveng_swapchain->extentAspectRatio(); }
		int getPixelValueFromPos(unsigned int mMouseXPos, unsigned int mMouseYPos) { return aveng_swapchain->getPixelValueFromPos(mMouseXPos, mMouseYPos, currentImageIndex); };
		uint32_t getImageCount() const { return aveng_swapchain->imageCount(); }
		void updateBufferViews();
		VkRenderPass getSwapChainRenderPass() const { return aveng_swapchain->getRenderPass(); }
		VkRenderPass getSelectionRenderPass() const { return renderData.rdSelectionRenderpass; }
		VkRenderPass getLineRenderPass() const { return renderData.rdLineRenderpass; }

		const PointLightData getPointLightData() { return mPointLightData; }

		VkSwapchainKHR getVkSwapchain() const { return aveng_swapchain->getSwapchain(); }
		VkFormat getSwapChainImageFormat() { return aveng_swapchain->getSwapChainImageFormat(); }
		VkFramebuffer getCurrentFramebuffer() { return aveng_swapchain->getFrameBuffer(currentImageIndex); }
		VkFramebuffer getCurrentSelectionFramebuffer() { return aveng_swapchain->getSelectionFrameBuffer(currentImageIndex); }

		bool createPipelineLayouts();
		bool createPipelines();
		bool createSSBOs();
		bool createMatrixUBO();
		bool createLightsUBO();
		bool createDefaultSamplers();

		bool createBindlessDescriptors();
		bool createBindlessDescriptorLayouts();
		bool createBindlessDescriptorSets();

		void updateBindlessDescriptorSets(int frameIndex);
		void updateTextureArrayDescriptorSet(TextureHandle handle, VkImageView view, VkSampler sampler, int frameIndex);

		int dispatchCompute(const IModelLibrary& modelLib, const FramePacket& pkt);

		/* ProcGen - TODO */
		void enqueueTerrainUpload(procgen::TerrainMeshCpu tMesh);

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

		bool createSyncObjects();

		// IRenderSceneView as an arg allows us to use different scene sources.
		// Alternatively you could store IRenderSceneView* but only if your engine guarantees it’s always valid and you prefer a slightly simpler callsite.
		int update(
			const FramePacket& pkt,
			const ModelLibrary& modelLib, // used to get Model pointers, *for now*
			float deltaTime);

		bool drawModels(
			const FramePacket& pkt,
			const IModelLibrary& modelLib_, // used to get Model pointers, *for now*
			VkCommandBuffer commandBuffer, 
			VkPipeline basicPipeline, 
			VkPipeline animationPipeline, 
			VkPipelineLayout basicLayout, 
			VkPipelineLayout animationLayout, 
			VkDescriptorSet basicDescriptorSet,
			VkDescriptorSet animationDescriptorSet,
			int frameIndex);

		bool drawModelsBindless(
			const FramePacket& pkt,
			const IModelLibrary& modelLib,
			VkCommandBuffer commandBuffer,
			VkPipeline basicPipeline,
			VkPipeline animationPipeline,
			int frameIndex);

		void reset_timers();

		bool beginFrame(); // Synchronization
		void endFrame(); // Synchronization
		void beginSwapChainRenderPass(VkCommandBuffer commandBuffer, VkFramebuffer frameBuffer, VkRenderPass renderpass, bool selection = false);
		void endSwapChainRenderPass(VkCommandBuffer commandBuffer);

		// New methods for descriptor/buffer management
		void updateCamera();

		void runComputeShaders(const AvengModel* model, int numInstances, uint32_t modelOffset, uint32_t skinMetaOffset, uint32_t numberOfBones);
		
		void initializePointLights();
		void renderLights(const VkPipeline pipeline);
		int getLightCount() const { return mPointLightData.numLights; }
		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius);
		void clearLights();

		// Newest methods:
		void beginGraphicsCommands(int currentFrameIndex);
		void endGraphicsCommands(int currentFrameIndex);
		void recreateSwapChain();
		bool isRecreatingSwapChain() { return recreatingSwapchain; }

		const VkPipeline pointLightPipeline() { return pointLightSystem.getPipeline(); }
		
		void destroyTrash();
		void cleanup();

		// New Resource Methods 

		/**
		 * Textures
		 */
		TextureRef new_texture(EngineDevice& engineDevice, TextureRef& ref, bool generateMipmaps, bool flipImage);
		bool gpu_upload_texture(EngineDevice& engineDevice, VkTextureStagingBuffer stagingData, uint32_t width, uint32_t height, 
								bool generateMipmaps, uint32_t mipmapLevels);
		void updatePGStorageImageDescriptor(int frameIndex);
	private:

		void createCommandBuffers();
		void freeCommandBuffers();
		size_t calculateDynamicUBOStride() const;

		const IModelQuery& modelQuery_;
		IModelAnimQuery& animQuery_;

		VkRenderData& renderData;

		bool firstFrame = true;
		bool mRenderpassBypass = false;
		bool recreatingSwapchain = false;
		size_t boneMatrixBufferSize;

		// Engine systems
		AvengWindow& aveng_window;
		EngineDevice& engineDevice;
		CameraManager& cameraManager;

		VkResult err;
		VkResult result;

		Timer mDrawTimer{};
		Timer mComputeTimer{};
		Timer mUploadToUBOTimer{};
		Timer mUploadToSSBO1Timer{};
		Timer mUploadToSSBO2Timer{};

		std::unique_ptr<SwapChain> aveng_swapchain;			// Swapchain - Heap Allocated makes it easier to rebuild when the window resizes
		PointLightSystem pointLightSystem{ engineDevice, renderData };	// Light stuff
		
		// Dynamic texture array support
		uint32_t currentImageIndex{ 0 };
		int currentFrameIndex{ 0 }; // Not tied to the image index
		bool isFrameStarted{ false };

		// Descriptors and Buffers
		std::vector<VkUniformBufferData> mPerspectiveViewMatrixUBOBuffers;
		std::vector<VkUniformBufferData> mPointLightUBOBuffers;
		std::vector<VkShaderStorageBufferData> mModelMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mShaderBoneMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mShaderTrsMatrixBuffers;
		std::vector<VkShaderStorageBufferData> mNodeTransformBuffers;
		std::vector<PendingBufferDestroy> buffer_trash;

		// Perspective, View
		VkUploadMatrices mMatrices{ glm::mat4(1.0f), glm::mat4(1.0f) };

		VkPushConstants mModelPushConst{};
		VkComputePushConstants mComputeModelData{};

		PointLightData mPointLightData{};

	};

} 