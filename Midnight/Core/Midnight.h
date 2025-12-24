#pragma once
#include "Core/Input/InputSystem.h"
#include "Core/aveng_window.h"
#include "CoreVK/EngineDevice.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/AvengFrame.h"
#include "Game/data.h"
#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif
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

		// Editor Only
		const AppMode mode() { return game_data.currentAppMode; }
		// Editor Only

		const VkDevice device() { return engineDevice.device(); }

		void render(float frameTime);
		void updateCamera(float frameTime);
		int registerCamera(std::string name, std::unique_ptr<ICameraDriver> cameraDriver);
		void setActiveCamera(int id) { cameraManager.setActive(id); }
		const int& activeCameraId() const { return cameraManager.activeId(); }

		// Resource State
		bool frameInProgress() { return renderer.isFrameInProgress(); }
		bool shouldClose() { return aveng_window.shouldClose(); }
		float getAspectRatio() { return renderer.getAspectRatio(); }

#ifdef ENABLE_EDITOR
		void updateGUI(const InputState& state);
		const EditorData&	editorData() const	{ return editor.data(); }
		EditorData&			editorData()		{ return editor.data(); }
#endif
	private:

		float aspect;
		GameData& game_data;
		GameInput gameInput; // Not exactly useful at the moment - game gets a const ref of InputState 

		CameraManager cameraManager;
		
		// Move these to Midnight
		AvengWindow aveng_window{ WIDTH, HEIGHT, "MIDNIGHT ENGINE" };
		EngineDevice engineDevice{ aveng_window }; // Summon things to this world
		VkRenderData renderData;
		ModelAndInstanceData mModelInstanceData{};

		// Engine & Renderer
		Renderer renderer{ engineDevice, aveng_window, renderData, mModelInstanceData, cameraManager };

#ifdef ENABLE_EDITOR

		Editor editor{ renderData, renderer, game_data, engineDevice, aveng_window, mModelInstanceData, cameraManager };
		EditorInput editorInput{ &editor };
		EditorGameRouter inputRouter{ game_data.currentAppMode, editorInput, gameInput };
		AvengFrame frame{ renderer, renderData, game_data, engineDevice, mModelInstanceData, &editor };
		InputSystem inputSystem{ inputRouter, game_data };
#else
		AvengFrame frame{ renderer, renderData, game_data, engineDevice, mModelInstanceData, nullptr };
		InputSystem inputSystem{ gameInput, game_data };
#endif

	};

}