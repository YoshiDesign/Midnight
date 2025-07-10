#pragma once
#include <memory>
// #include "../Scene/app_object.h"
#include "Game/aveng_frame_content.h"
#include "System/Render/AvengImageSystem.h"
#include "System/Render/PointLightSystem.h"
#include "Core/Renderer/Renderer.h"
#include "System/Peripheral/KeyboardController.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/GFXPipeline.h"
#include "CoreVK/aveng_descriptors.h"
#include "Game/data.h"
#include "avpch.h"

namespace aveng {

	class ObjectRenderSystem {

		struct GlobalUbo {
			glm::mat4 projection{ 1.f };
			glm::mat4 view{ 1.f };
			glm::vec4 ambientLightColor{ 0.f, 0.f, 1.f, .14f };
			glm::vec3 lightPosition{ 5.0f, -20.0f, 2.8f };
			alignas(16) glm::vec4 lightColor{ 1.f, 1.f, 1.f, 1.f };
			//alignas(16) glm::vec3 lightDirection = glm::normalize(glm::vec3{ -1.f, -3.f, 1.f });
		};

		struct ObjectUniformData {
			//  Data alignment must be a multiple of VkPhysicalDeviceLimits::minUniformBufferOffsetAlignment
			alignas(16) int texIndex;
		};
	public:
		//ObjectRenderSystem();
		ObjectRenderSystem(EngineDevice& device, AvengAppObject& viewer, AvengWindow& window);
		~ObjectRenderSystem();

		ObjectRenderSystem(const ObjectRenderSystem&) = delete;
		void initialize(VkRenderPass renderPass, VkDescriptorSetLayout globalDescriptorSetLayout, VkDescriptorSetLayout fragDescriptorSetLayouts);
		void descriptorSetup();
		void addObjects(AvengAppObject);

		ObjectRenderSystem& operator=(const ObjectRenderSystem&) = delete;
		void render(FrameContent& frame_content, GameData& data /*, AvengBuffer& fragBuffer*/);
		VkPipelineLayout getPipelineLayout() { return pipelineLayout; }
		float getAspectRatio() { return renderer.getAspectRatio(); }
		void DependencyChecks();
		void setNumObjects(int n) { num_objects = n; }
		Renderer& pRenderer() { return renderer; }

	private:

		void createPipelineLayout(VkDescriptorSetLayout* descriptorSetLayouts);
		void updateData(size_t size, float frameTime, GameData& data);
		void createPipeline(VkRenderPass renderPass);

		int last_sec;
		int num_objects;
		//EngineDevice &engineDevice;
		//AvengAppObject& viewerObject;

		// Rendering Pipelines - Heap Allocated
		std::unique_ptr<GFXPipeline> gfxPipeline;
		std::unique_ptr<GFXPipeline> gfxPipeline2;
		VkPipelineLayout pipelineLayout;
		AvengWindow& aveng_window;
		EngineDevice& engineDevice;
		AvengAppObject& viewerObject;
		// size_t deviceAlignment = engineDevice.properties.limits.minUniformBufferOffsetAlignment;

		Renderer renderer{ aveng_window, engineDevice };
		ImageSystem imageSystem{ engineDevice };
		
		/*ObjectRenderSystem objectRenderSystem{ engineDevice, viewerObject };*/
		PointLightSystem pointLightSystem{ engineDevice };
		GlobalUbo u_GlobalData{};

		std::unique_ptr<AvengDescriptorPool> descriptorPool{};
		std::vector<std::unique_ptr<AvengBuffer>> u_GlobalBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_ObjBuffers;
		std::vector<VkDescriptorSet> globalDescriptorSets;
		std::vector<VkDescriptorSet> objectDescriptorSets;

	};

}