#pragma once

#include <memory>
#include <vector>
#include <cassert>
#include "../aveng_window.h"
#include "../../CoreVK/EngineDevice.h"
#include "../../CoreVK/swapchain.h"
#include "../../CoreVK/GFXPipeline.h"
#include "../../CoreVK/aveng_descriptors.h"
#include "../../CoreVK/aveng_buffer.h"
#include "../../CoreVK/AvengImageSystem.h"
#include "../../CoreVK/PointLightSystem.h"
#include "../aveng_model.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace aveng {

	// Forward declarations
	class AvengAppObject;

	// Data structures moved from ObjectRenderSystem
	struct PointLight {
		glm::vec4 position{ 0.f, 0.f, 0.f, 1.f };  // w can be used for radius
		glm::vec4 color{ 1.f, 1.f, 1.f, 1.f };     // w is intensity
	};

	struct LightsUbo {
		static constexpr int MAX_LIGHTS = 100;
		uint32_t numLights{ 0 };
		alignas(16) PointLight lights[MAX_LIGHTS];
	};

	struct GlobalUbo {
		glm::mat4 projection{ 1.f };
		glm::mat4 view{ 1.f };
		glm::vec4 ambientLightColor{ 0.f, 0.f, 1.f, .14f };
		glm::vec3 lightPosition{ 5.0f, -20.0f, 2.8f };
		alignas(16) glm::vec4 lightColor{ 1.f, 1.f, 1.f, 1.f };
	};

	struct ObjectUniformData {
		alignas(16) int texIndex;
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
		void setupDescriptors(int numObjects);
		void updateFrameData(const GlobalUbo& globalData, const LightsUbo& lightsData);
		void renderObjects(const std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>>& objectData);
		void renderLights(int numLights);

	private:

		VkResult err;

		void createCommandBuffers();
		void freeCommandBuffers();
		void recreateSwapChain();

		// Moved from ObjectRenderSystem
		void createPipelineLayout();
		void createPipeline();
		size_t calculateDynamicUBOStride() const;

		AvengWindow& aveng_window;
		EngineDevice& engineDevice;

		std::vector<VkCommandBuffer> commandBuffers;

		// SwapChain aveng_swapchain{ engineDevice, aveng_window.getExtent() };	// previous stack allocated. Ptr makes it easier to rebuild when the window resizes
		std::unique_ptr<SwapChain> aveng_swapchain;
		
		uint32_t currentImageIndex{0};
		int currentFrameIndex; // Not tied to the image index
		bool isFrameStarted{ false };

		// Moved from ObjectRenderSystem - Engine resources
		std::unique_ptr<GFXPipeline> gfxPipeline;
		std::unique_ptr<GFXPipeline> gfxPipeline2;
		VkPipelineLayout pipelineLayout{};

		// Descriptor and Buffer management
		std::unique_ptr<AvengDescriptorPool> descriptorPool{};
		std::vector<std::unique_ptr<AvengBuffer>> u_GlobalBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_ObjBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_LightsBuffers;
		std::vector<VkDescriptorSet> globalDescriptorSets;
		std::vector<VkDescriptorSet> objectDescriptorSets;
		std::vector<VkDescriptorSet> lightsDescriptorSets;

		// Engine systems
		ImageSystem imageSystem;
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