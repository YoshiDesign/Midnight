#pragma once
#include <memory>
// #include "../Scene/app_object.h"
#include "Core/aveng_frame_content.h"
#include "System/Render/AvengImageSystem.h"
#include "System/Render/PointLightSystem.h"
#include "System/Camera/aveng_camera.h"
#include "Core/Renderer/Renderer.h"
#include "System/Peripheral/KeyboardController.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/GFXPipeline.h"
#include "CoreVK/aveng_descriptors.h"
#include "Utils/SystemData.h"
#include "Core/data.h"
#include "avpch.h"

#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif

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
		ObjectRenderSystem(EngineDevice& device, AvengWindow& window);
		~ObjectRenderSystem();
		ObjectRenderSystem& operator=(const ObjectRenderSystem&) = delete;
		ObjectRenderSystem(const ObjectRenderSystem&) = delete;

		void initialize(VkRenderPass renderPass, VkDescriptorSetLayout globalDescriptorSetLayout, VkDescriptorSetLayout fragDescriptorSetLayouts);
		VkPipelineLayout getPipelineLayout() { return pipelineLayout; }
		void descriptorSetup();
		void addObjects(AvengAppObject);
		float getAspectRatio() { return renderer.getAspectRatio(); }
		void setNumObjects(int n) { num_objects = n; } // REQUIRED - This number is used when initializing buffers

		void render(float frameTime);
		void DependencyChecks();
		void updateCamera(float frameTime, AvengAppObject& viewerObject, KeyboardController& keyboardController);

		// Renderer& pRenderer() { return renderer; }
		SystemContext& context() { return systemData.systemContext(); };

	private:

		void createPipelineLayout(VkDescriptorSetLayout* descriptorSetLayouts);
		void updateData(float frameTime);
		void createPipeline(VkRenderPass renderPass);

		int last_sec;
		int num_objects{1};
		float aspect;

		

		// Rendering Pipelines - Heap Allocated
		std::unique_ptr<GFXPipeline> gfxPipeline;
		std::unique_ptr<GFXPipeline> gfxPipeline2;

		AvengAppObject::Map appObjects;
		AvengAppObject viewerObject{ AvengAppObject::createAppObject(1000) };
		VkPipelineLayout pipelineLayout;

		AvengWindow& aveng_window;
		EngineDevice& engineDevice;
		GameData game_data;
		AvengCamera aveng_camera;
		KeyboardController keyboardController{ viewerObject, game_data };
		Renderer renderer{ aveng_window, engineDevice };
		ImageSystem imageSystem{ engineDevice };
		PointLightSystem pointLightSystem{ engineDevice };
		GlobalUbo u_GlobalData{};
		SystemData systemData{ engineDevice, aveng_window, aveng_camera, renderer, game_data, appObjects };
		

#ifdef ENABLE_EDITOR
		aveng::Editor editor{systemData.systemContext()};
#endif
		
		std::unique_ptr<AvengDescriptorPool> descriptorPool{};
		std::vector<std::unique_ptr<AvengBuffer>> u_GlobalBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_ObjBuffers;
		std::vector<VkDescriptorSet> globalDescriptorSets;
		std::vector<VkDescriptorSet> objectDescriptorSets;

	};

}