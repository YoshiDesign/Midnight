#pragma once
#include "System/Interface/IInputHandler.h"
#include "CoreVK/EngineDevice.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/AvengFrame.h"
#include "System/Camera/aveng_camera.h"
#include "CoreVK/VkRenderData.h"
#include "System/InputSystem.h"
#include "System/Peripheral/KeyboardController.h"
#include "Game/data.h"
#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif
#include "avpch.h"

namespace aveng {

	class AvengWindow;

	// Use engine types
	using LightsUbo = aveng::LightsUbo;
	// using GlobalUbo = aveng::GlobalUbo;
	using ObjectUniformData = aveng::ObjectUniformData;

	class ObjectRenderSystem {
	public:
		//ObjectRenderSystem();
		explicit ObjectRenderSystem(AvengWindow& _window);
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

		VkDevice getEngineDevice() { return engineDevice.device(); }

	private:

#ifdef ENABLE_EDITOR
		AppMode mode_ = AppMode::Editor;
#else
		AppMode mode_ = AppMode::Game;
#endif
		void updateData(float frameTime);

		bool firstFrame;
		int last_sec;
		float aspect;
		int frameIndex;

		int curCamera = 1; // Tmp

		AvengWindow& window;				 // GLHF
		EngineDevice engineDevice{ window }; // Summon things to this world
		AvengCamera player_camera;
		
		// Game & State
		GameData gameData;
		Game holyShip{ gameData };
		VkRenderData renderData;
		ModelAndInstanceData mModelInstanceData{};

		// Application-level components
		AvengAppObject viewerObject{ AvengAppObject::createAppObject(1000) };
		KeyboardController keyboardController{ viewerObject, gameData };

		// Engine & Renderer
		Renderer renderer{ engineDevice, window, renderData, mModelInstanceData };
		GameInput gameInput;

#ifdef ENABLE_EDITOR
		EditorInput editorInput;
		EditorGameRouter inputRouter;
		InputSystem inputSystem;

		Editor editor{ renderData, renderer, gameData, engineDevice, window, mModelInstanceData };
		AvengFrame frame{renderer, renderData, gameData, engineDevice, mModelInstanceData, &editor };
#else
		InputSystem inputSystem;
		AvengFrame frame{ renderer, renderData, gameData, engineDevice, mModelInstanceData, nullptr };
#endif

	};

}