#pragma once
#include <memory>
#include "Core/aveng_frame_content.h"
#include "Core/Renderer/Renderer.h"
#include "System/Camera/aveng_camera.h"
#include "System/Peripheral/KeyboardController.h"
#include "CoreVK/EngineDevice.h"
#include "Core/data.h"
#include "CoreVK/VkRenderData.h"
#include "avpch.h"

namespace aveng {

	// Use engine types
	using LightsUbo = aveng::LightsUbo;
	using GlobalUbo = aveng::GlobalUbo;
	using ObjectUniformData = aveng::ObjectUniformData;

	class ObjectRenderSystem {
	public:
		//ObjectRenderSystem();
		ObjectRenderSystem(AvengWindow& _window);
		~ObjectRenderSystem();
		ObjectRenderSystem& operator=(const ObjectRenderSystem&) = delete;
		ObjectRenderSystem(const ObjectRenderSystem&) = delete;

		// Application-level interface
		void initialize();
		void loadGame(const std::string& scenePath);
		float getAspectRatio() { return renderer.getAspectRatio(); }

		// Light management
		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f, float radius = 0.1f);

		// Main render function - simplified interface
		void render(float frameTime);

		VkDevice engineDevice() { return renderer.getEngineDevice(); }
		
		// Application-specific updates
		void updateCamera(float frameTime);

		// Renderer& pRenderer() { return renderer; }
		// SystemContext& context() { return systemData.systemContext(); };

	private:

		void updateData(float frameTime);
		void updatePostProcessing(float frameTime);

		bool firstFrame;
		int last_sec;
		float aspect;
		
		// Store animated instances for rendering
		std::vector<std::shared_ptr<class AssimpInstance>> animatedInstances;

		// Application-level components
		AvengAppObject viewerObject{ AvengAppObject::createAppObject(1000) };
		
		// Application state
		GameData game_data;
		AvengCamera aveng_camera;
		AvengWindow& window;
		KeyboardController keyboardController{ viewerObject, game_data };
		VkRenderData renderData;
		
		// Engine renderer (now owns all Vulkan resources)
		Renderer renderer{ window, game_data };

	};

}