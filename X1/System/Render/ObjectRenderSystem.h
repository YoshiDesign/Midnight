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

		/**
		* Pro Tip:
		* Radius in position.w, intensity in color.w saves space.
		* 
		*/

		// Individual point light data
		struct PointLight {
			glm::vec4 position{ 0.f, 0.f, 0.f, 1.f };  // w can be used for radius
			glm::vec4 color{ 1.f, 1.f, 1.f, 1.f };     // w is intensity
		};

		// Lights uniform buffer for multiple lights
		struct LightsUbo {
			static constexpr int MAX_LIGHTS = 100;
			uint32_t numLights{ 0 };
			alignas(16) PointLight lights[MAX_LIGHTS]; // Cache locality win
		};

		struct GlobalUbo {
			glm::mat4 projection{ 1.f };							// 64 bytes
			glm::mat4 view{ 1.f };									// 64 bytes
			glm::vec4 ambientLightColor{ 0.f, 0.f, 1.f, .14f };		// 32 bytes
			glm::vec3 lightPosition{ 5.0f, -20.0f, 2.8f };			// 24 bytes
			alignas(16) glm::vec4 lightColor{ 1.f, 1.f, 1.f, 1.f };	// 32 bytes
			// glm::vec3 lightDirection = glm::normalize(glm::vec3{ -1.f, -3.f, 1.f });
		};

		/**
		* Per-object uniform. This one is passed directly to the Fragment Shader
		*/
		// layout(set = 1, binding = 0) uniform ObjectUniformData {
		// 	 uint texIndex;
		// } u_ObjData;
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

		// Light management
		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f, float radius = 0.1f);
		void clearLights();
		int getLightCount() const { return u_LightsData.numLights; }

		void render(float frameTime, FrameContent& frameContent);
		void DependencyChecks();
		void updateCamera(float frameTime, AvengAppObject& viewerObject, KeyboardController& keyboardController);

		// Renderer& pRenderer() { return renderer; }
		SystemContext& context() { return systemData.systemContext(); };

	private:

		void createPipelineLayout(VkDescriptorSetLayout* descriptorSetLayouts);
		void updateData(float frameTime);
		void createPipeline(VkRenderPass renderPass);
		
		// Helper function to calculate dynamic uniform buffer stride
		size_t calculateDynamicUBOStride() const;

		int last_sec;
		int num_objects{1}; // TODO - This will cause a crash if it's 0, not idea but not a real issue yet
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
		LightsUbo u_LightsData{};
		SystemData systemData{ engineDevice, aveng_window, aveng_camera, renderer, game_data, appObjects };
		

#ifdef ENABLE_EDITOR
		aveng::Editor editor{systemData.systemContext()};
#endif
		
		std::unique_ptr<AvengDescriptorPool> descriptorPool{};
		std::vector<std::unique_ptr<AvengBuffer>> u_GlobalBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_ObjBuffers;
		std::vector<std::unique_ptr<AvengBuffer>> u_LightsBuffers;
		std::vector<VkDescriptorSet> globalDescriptorSets;
		std::vector<VkDescriptorSet> objectDescriptorSets;
		std::vector<VkDescriptorSet> lightsDescriptorSets;

	};

}