#pragma once
#include "Core/Input/InputSystem.h"
#include "Core/aveng_window.h"
#include "CoreVK/EngineDevice.h"
#include "Core/aveng_window.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/AvengFrame.h"
#include "Game/data.h"
#include "Editor.h"
#include "avpch.h"

namespace aveng {

	class Midnight {

	static constexpr int WIDTH = 2080;
	static constexpr int HEIGHT = 960;

	public:
		Midnight(GameData& _gamedata);
		~Midnight() = default;

		void beginFrameInput() { inputSystem.beginFrame(); }

		const InputState& inputState() { return inputSystem.inputState(); }
		const AppMode mode() { return mode_; }

		const VkDevice device() { return engineDevice.device(); }
		float getAspectRatio() { return renderer.getAspectRatio(); }

		void render(float frameTime) { frame.render(frameTime); }
		void updateCamera(glm::mat4 proj, glm::mat4 view);

		bool shouldClose() { return aveng_window.shouldClose(); }

	private:

		GameData& game_data;

#ifdef ENABLE_EDITOR
		AppMode mode_ = AppMode::Editor;
#else
		AppMode mode_ = AppMode::Game;
#endif
		GameInput gameInput;
		
		// Move these to Midnight
		AvengWindow aveng_window{ WIDTH, HEIGHT, "MIDNIGHT ENGINE" };			 // GLHF
		EngineDevice engineDevice{ aveng_window }; // Summon things to this world
		VkRenderData renderData;
		ModelAndInstanceData mModelInstanceData{};

		// Engine & Renderer
		Renderer renderer{ engineDevice, aveng_window, renderData, mModelInstanceData };

#ifdef ENABLE_EDITOR
		Editor editor{ renderData, renderer, game_data, engineDevice, aveng_window, mModelInstanceData };
		EditorInput editorInput{ &editor };
		EditorGameRouter inputRouter{ mode_, editorInput, gameInput };
		AvengFrame frame{ renderer, renderData, game_data, engineDevice, mModelInstanceData, &editor };
		InputSystem inputSystem{ inputRouter };
#else
		AvengFrame frame{ renderer, renderData, game_data, engineDevice, mModelInstanceData, nullptr };
		InputSystem inputSystem{ gameInput };
#endif

	};

}