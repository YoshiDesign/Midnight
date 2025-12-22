#pragma once
#include "Core/Midnight.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/AvengFrame.h"
#include "System/Camera/aveng_camera.h"
#include "Core/Input/IInputHandler.h"
#include "Core/Input/InputSystem.h"
#include "System/Peripheral/KeyboardController.h"
#include "Game/data.h"
#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif
#include "avpch.h"

namespace aveng {

	class AvengWindow;

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
		void updateData(float frameTime);

		bool firstFrame;
		int last_sec;
		float aspect;
		int frameIndex;

		int curCamera = 1; // Tmp
		AvengCamera player_camera;
		
		// Game & State
		GameData gameData;
		Game holyShip{ gameData };

		// Application-level components
		AvengAppObject viewerObject{ AvengAppObject::createAppObject(1000) };
		KeyboardController keyboardController{ viewerObject, gameData };

		// Move these to Midnight
		AvengWindow& window;				 // GLHF
		EngineDevice engineDevice{ window }; // Summon things to this world
		VkRenderData renderData;
		ModelAndInstanceData mModelInstanceData{};

		// Engine & Renderer
		Renderer renderer{ engineDevice, window, renderData, mModelInstanceData };

#ifdef ENABLE_EDITOR
		Editor editor{ renderData, renderer, gameData, engineDevice, window, mModelInstanceData };
		AvengFrame frame{renderer, renderData, gameData, engineDevice, mModelInstanceData, &editor };
#else
		AvengFrame frame{ renderer, renderData, gameData, engineDevice, mModelInstanceData, nullptr };
#endif

		Midnight midnight{editor, window};

	};

}