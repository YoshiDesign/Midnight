#pragma once

#include <memory>
#include <vector>
#include <cassert>
#include <unordered_map>
#include "../aveng_window.h"
#include "../../CoreVK/EngineDevice.h"
#include "../../CoreVK/swapchain.h"
#include "../../CoreVK/GFXPipeline.h"
#include "../../CoreVK/aveng_descriptors.h"
#include "../../CoreVK/aveng_buffer.h"
#include "../../CoreVK/AvengImageSystem.h"
#include "../../CoreVK/PointLightSystem.h"
#include "../aveng_model.h"
#include "../data.h"
#include "PipelineConfigManager.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace aveng {

	// Forward declarations
	class AvengAppObject;

	// Pipeline management enums
	enum class ObjectRenderMode {
		STANDARD = 0,
		WIREFRAME = 1,
		DISTORTED = 2,
		// Add more as needed
	};

	enum class PostProcessMode {
		NONE = 0,
		TOXIC_CLOUD = 1,
		NIGHT_VISION = 2,
		CHROMATIC_ABERRATION = 3,
		// Add more effects
	};

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
		glm::vec4 ambientLightColor{ 0.f, 0.f, 1.f, .14f };
		glm::vec3 lightPosition{ 5.0f, -20.0f, 2.8f };
		alignas(16) glm::vec4 lightColor{ 1.f, 1.f, 1.f, 1.f };
		alignas(16) int renderMode{ 0 };  // 0 = STANDARD, 1 = WIREFRAME, 2 = DISTORTED
		alignas(16) float time{ 0.0f };   // For animated effects
	};

	struct ObjectUniformData {
		alignas(16) int texIndex;
	};

	// Instance data for instanced rendering - per object instance
	struct InstanceData {
		alignas(16) glm::mat4 modelMatrix;
		alignas(16) glm::mat4 normalMatrix;
		alignas(16) int textureIndex;
		alignas(16) int padding[3]; // Ensure 16-byte alignment
	};

	// Batch data for grouping instances by model
	struct RenderBatch {
		AvengModel* model;
		std::vector<InstanceData> instances;
		std::unique_ptr<AvengBuffer> instanceBuffer;
		
		RenderBatch(AvengModel* m) : model(m) {}
	};

	class Renderer {

	public:

		Renderer(AvengWindow &window, EngineDevice &device);
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer &operator=(const Renderer&) = delete;

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

		// SwapChain getters
		uint32_t getImageCount() const { return aveng_swapchain->imageCount(); }
		VkImage& getImage(int index) { return aveng_swapchain->getImage(index); }
		VkFormat getSwapChainImageFormat() { return aveng_swapchain->getSwapChainImageFormat(); }

		VkCommandBuffer beginFrame();
		void endFrame();
		void beginSwapChainRenderPass(VkCommandBuffer commandBuffer, glm::vec3 rgb);
		void endSwapChainRenderPass(VkCommandBuffer commandBuffer);

		// New methods for descriptor/buffer management
		void initializeImageSystem(const std::vector<std::string>& texturePaths);
		void setupDescriptors(int numObjects);
		void initializePointLightSystem();
		void updateFrameData(const GlobalUbo& globalData, const LightsUbo& lightsData);
		void renderObjects(const std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>>& objectData);
		void renderLights(int numLights);

		// Instanced rendering methods
		void renderObjectsInstanced(const std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>>& objectData);
		void setupInstanceBuffers();
		void updateInstanceBuffer(RenderBatch& batch);

		// Instanced rendering controls
		void setInstancedRenderingEnabled(bool enabled) { instancedRenderingEnabled = enabled; }
		bool getInstancedRenderingEnabled() const { return instancedRenderingEnabled; }
		uint32_t getBatchCount() const { return static_cast<uint32_t>(renderBatches.size()); }

		// Pipeline switching methods
		void setObjectRenderMode(ObjectRenderMode mode) { currentObjectMode = mode; }
		void setPostProcessMode(PostProcessMode mode) { currentPostProcessMode = mode; }
		ObjectRenderMode getObjectRenderMode() const { return currentObjectMode; }
		PostProcessMode getPostProcessMode() const { return currentPostProcessMode; }
		
		// Pipeline management methods
		bool reloadPipelineConfig(const std::string& configPath = "");
		std::vector<std::string> getAvailablePipelines() const;

	private:
		// Dynamic texture array support
		uint32_t currentTextureCount = 8; // Track current texture count for pipeline creation
		bool pipelineCreated = false; // Guard against double pipeline creation

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
		void createPostProcessPipelines();
		
		// DEPRECATED: Legacy pipeline creation (to be removed)
		void createPipeline();                  // DEPRECATED: Creates legacy gfxPipeline
		void createObjectPipelines();           // DEPRECATED: Creates hardcoded pipeline array
		
		size_t calculateDynamicUBOStride() const;

		AvengWindow& aveng_window;
		EngineDevice& engineDevice;

		std::vector<VkCommandBuffer> commandBuffers;

		// SwapChain aveng_swapchain{ engineDevice, aveng_window.getExtent() };	// previous stack allocated. Ptr makes it easier to rebuild when the window resizes
		std::unique_ptr<SwapChain> aveng_swapchain;
		
		uint32_t currentImageIndex{0};
		int currentFrameIndex; // Not tied to the image index
		bool isFrameStarted{ false };

		// Pipeline management
		std::unique_ptr<PipelineConfigManager> pipelineManager;
		std::vector<std::unique_ptr<GFXPipeline>> objectPipelines;  // DEPRECATED: Fallback only
		// DEPRECATED: Remove these legacy pipelines - use PipelineConfigManager instead
		std::unique_ptr<GFXPipeline> gfxPipeline;  // Legacy - to be removed
		std::unique_ptr<GFXPipeline> gfxPipeline2; // Legacy - to be removed
		
		// Post-processing pipelines and resources
		std::vector<std::unique_ptr<GFXPipeline>> postProcessPipelines;  // Index corresponds to PostProcessMode
		VkRenderPass offscreenRenderPass{};
		std::vector<VkFramebuffer> offscreenFramebuffers;
		VkImage offscreenColorImage{};
		VkDeviceMemory offscreenColorImageMemory{};
		VkImageView offscreenColorImageView{};
		VkImage offscreenDepthImage{};
		VkDeviceMemory offscreenDepthImageMemory{};
		VkImageView offscreenDepthImageView{};
		
		VkPipelineLayout pipelineLayout{};
		VkPipelineLayout postProcessPipelineLayout{};

		// Descriptor and Buffer management
		std::unique_ptr<AvengDescriptorPool> descriptorPool{};
		std::vector<std::unique_ptr<AvengBuffer>> u_GlobalBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_ObjBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_LightsBuffers;
		
		// Animation buffers (SSBOs)
		std::vector<std::unique_ptr<AvengBuffer>> u_AnimationBuffers;           // Animation UBO data
		std::vector<std::unique_ptr<AvengBuffer>> boneMatrixBuffers;           // Bone transformation matrices SSBO
		std::vector<std::unique_ptr<AvengBuffer>> instanceAnimationBuffers;    // Instance animation data SSBO
		std::vector<std::unique_ptr<AvengBuffer>> animatedVertexBuffers;       // Animated vertex data SSBO
		std::vector<std::unique_ptr<AvengBuffer>> transformedVertexBuffers;    // Compute shader output SSBO
		std::vector<VkDescriptorSet> globalDescriptorSets;
		std::vector<VkDescriptorSet> objectDescriptorSets;
		std::vector<VkDescriptorSet> lightsDescriptorSets;

		// Post-processing descriptor sets
		std::vector<VkDescriptorSet> postProcessDescriptorSets;
		
		// Animation descriptor sets
		std::vector<VkDescriptorSet> animationDescriptorSets;

		// Descriptor set layouts (reused by multiple systems)
		std::unique_ptr<AvengDescriptorSetLayout> globalDescriptorSetLayout;
		std::unique_ptr<AvengDescriptorSetLayout> objDescriptorSetLayout;
		std::unique_ptr<AvengDescriptorSetLayout> lightsDescriptorSetLayout;
		std::unique_ptr<AvengDescriptorSetLayout> postProcessDescriptorSetLayout;
		std::unique_ptr<AvengDescriptorSetLayout> animationDescriptorSetLayout;

		// Instanced rendering data
		std::unordered_map<AvengModel*, std::unique_ptr<RenderBatch>> renderBatches;
		bool instancedRenderingEnabled = true; // Toggle for A/B testing
		uint32_t maxInstancesPerBatch = 1000;  // Reasonable default

		// Engine systems
		std::unique_ptr<ImageSystem> imageSystem;
		PointLightSystem pointLightSystem;

		// State
		int num_objects{1};

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