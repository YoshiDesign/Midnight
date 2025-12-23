#pragma once
#include "Core/Midnight.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/AvengFrame.h"
#include "Core/Camera/aveng_camera.h"
#include "Core/Input/IInputHandler.h"
#include "Core/Input/InputSystem.h"
#include "System/Peripheral/KeyboardController.h"
#include "Game/Camera/CameraManager.h"
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
		explicit ObjectRenderSystem();
		~ObjectRenderSystem();
		ObjectRenderSystem& operator=(const ObjectRenderSystem&) = delete;
		ObjectRenderSystem(const ObjectRenderSystem&) = delete;

		// Application-level interface
		void initialize();
		void loadGame(const std::string& scenePath);
		float getAspectRatio() { return midnight.getAspectRatio(); }

		// Light management
		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f, float radius = 0.1f);

		// Main render function - simplified interface
		void render(float frameTime);

		// Application-specific updates
		void updateCamera(float frameTime, const InputState& state);

		VkDevice getEngineDevice() { return midnight.device(); }

		bool shouldClose() { return midnight.shouldClose(); }

	private:

		int last_sec;
		float aspect;
		int frameIndex;
		int player_camera_id;

		// Game & State
		GameData gameData;
		Game holyShip{ gameData };
		Midnight midnight{ gameData };

	};

}