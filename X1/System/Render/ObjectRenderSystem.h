#pragma once
#include <memory>
#include "Core/aveng_frame_content.h"
#include "Core/Renderer/Renderer.h"
#include "System/Camera/aveng_camera.h"
#include "System/Peripheral/KeyboardController.h"
#include "CoreVK/EngineDevice.h"
#include "Utils/SystemData.h"
#include "Core/data.h"
#include "Core/aveng_scene_loader.h"
#include "avpch.h"

#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif

namespace aveng {

	// Use engine types
	using LightsUbo = aveng::LightsUbo;
	using GlobalUbo = aveng::GlobalUbo;
	using ObjectUniformData = aveng::ObjectUniformData;

	class ObjectRenderSystem {
	public:
		//ObjectRenderSystem();
		ObjectRenderSystem(EngineDevice& device, AvengWindow& window);
		~ObjectRenderSystem();
		ObjectRenderSystem& operator=(const ObjectRenderSystem&) = delete;
		ObjectRenderSystem(const ObjectRenderSystem&) = delete;

		// Application-level interface
		void initialize();
		void loadGame(const std::string& scenePath);
		float getAspectRatio() { return renderer.getAspectRatio(); }

		// Light management
		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f, float radius = 0.1f);
		void clearLights();
		int getLightCount() const { return u_LightsData.numLights; }

		// Main render function - simplified interface
		void render(float frameTime, FrameContent& frameContent);
		
		// Animation system integration  
		void updateAnimationData(const std::vector<std::shared_ptr<class AssimpInstance>>& instances, float deltaTime);
		
		// Application-specific updates
		void updateCamera(float frameTime, AvengAppObject& viewerObject, KeyboardController& keyboardController);

		// Renderer& pRenderer() { return renderer; }
		SystemContext& context() { return systemData.systemContext(); };

	private:

		void updateData(float frameTime);
		void updatePostProcessing(float frameTime);

		bool firstFrame;
		int last_sec;
		float aspect;
		
		// Store animated instances for rendering
		std::vector<std::shared_ptr<class AssimpInstance>> animatedInstances;

		// Application-level components
		AvengSceneLoader sceneLoader;
		AvengAppObject viewerObject{ AvengAppObject::createAppObject(1000) };

		// Core engine and window references
		AvengWindow& aveng_window;
		EngineDevice& engineDevice;
		
		// Application state
		GameData game_data;
		AvengCamera aveng_camera;
		KeyboardController keyboardController{ viewerObject, game_data };
		
		// Engine renderer (now owns all Vulkan resources)
		Renderer renderer{ aveng_window, engineDevice };
		
		// Application data
		GlobalUbo u_GlobalData{};
		LightsUbo u_LightsData{};
		
		// System context for editor
		SystemData systemData{ engineDevice, aveng_window, aveng_camera, renderer, game_data, sceneLoader.getAppObjects() };

#ifdef ENABLE_EDITOR
		aveng::Editor editor{systemData.systemContext()};
#endif

	};

}