#pragma once
#include <memory>
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"
#include "Core/Renderer/Renderer.h"
#include "Core/aveng_frame_content.h"
#include "Core/CameraProxy.h"
#include "Core/data.h"
#include "Editor.h"
#include "System/Camera/aveng_camera.h"
#include "System/Peripheral/KeyboardController.h"
#include "avpch.h"

namespace aveng {

	// Use engine types
	using LightsUbo = aveng::LightsUbo;
	// using GlobalUbo = aveng::GlobalUbo;
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
		int frameIndex;

		AvengWindow& window;				 // GLHF
		EngineDevice engineDevice{ window }; // Summon things to this world
		AvengCamera aveng_camera;
		KeyboardController keyboardController{ viewerObject, game_data };
		std::shared_ptr<CameraProxy> camProxy;

		// State
		GameData game_data;
		VkRenderData renderData;
		ModelAndInstanceData mModelInstanceData{};

		// Application-level components
		AvengAppObject viewerObject{ AvengAppObject::createAppObject(1000) };
		
		// Engine renderer (now owns all Vulkan resources)
		Renderer renderer{ engineDevice, window, renderData, game_data, mModelInstanceData };

#ifdef ENABLE_EDITOR
		Editor editor{ renderData, renderer, game_data, engineDevice, window, mModelInstanceData };
#endif

	};

}