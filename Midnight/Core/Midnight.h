#pragma once

#include "Core/Input/InputSystem.h"
#include "Core/aveng_window.h"
#include "CoreVK/EngineDevice.h"
#include "Core/Renderer/ModelLibrary.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/AvengFrame.h"
#include "Core/Imaging/MidnightTextureSystem.h"
#include "Runtime/Threading/ITaskSystem.h"
#include "Runtime/Facade/SceneFacade.h"
#include "Runtime/Play/Controller/TerrainController.h"
#include "Runtime/Play/Controller/DebugController.h"
#include "Runtime/Play/GameContext.h"
#include "Module/Procgen/Terrain/ChunkManager2.h"
#include "Runtime/Memory/Arena.h"
	
#include "Game/data.h"
#ifdef ENABLE_EDITOR
#include "Editor/Editor.h"
#endif

namespace aveng {

	class Midnight {

	static constexpr int WIDTH = 2080;
	static constexpr int HEIGHT = 960;

	public:
		Midnight(GameData& _gamedata);
		~Midnight();

		const GameServices& gameServices() const noexcept { return gameServices_; };

		void initialize();
		void initializeDependencies();

		void beginFrameInput() { inputSystem_->beginFrame(); }

		const InputState& inputState() { return inputSystem_->inputState(); }

		// Editor Only -  currently
		const AppMode mode() { return game_data.currentAppMode; }
		// Editor Only

		std::unique_ptr<IAssetSource> createModelSource();

		const VkDevice device() { return engineDevice.device(); }

		void render(float frameTime);
		void updateCamera(float frameTime);
		int registerCamera(std::string name, std::unique_ptr<ICameraDriver> cameraDriver);
		void setActiveCamera(int id) { cameraManager.setActive(id); }
		const int& activeCameraId() const { return cameraManager.activeId(); }
		const AvengCamera& camera() const { return cameraManager.active().camera; }

		// Resource State
		bool frameInProgress() { return renderer.isFrameInProgress(); }
		int frameIndex() { return renderer.getFrameIndex(); }
		bool shouldClose() { return aveng_window.shouldClose(); }
		float getAspectRatio() { return renderer.getAspectRatio(); }

		void registerCallbacks();

#ifdef ENABLE_EDITOR
		void updateGUI(const InputState& state);
		const EditorData&	editorData() const	{ return editor_->data(); }
		EditorData&			editorData()		{ return editor_->data(); }
#endif

		void shutdown();

	private:
		float aspect = 0.0f; // ?

		// Primary Threadpool
		ThreadPoolTaskSystem taskSystem_;

		// Data
		GameData& game_data;
		VkRenderData renderData{};

		Arena* terrain_arena;
		Arena* frame_arena;

		// State
		AvengWindow  aveng_window{ WIDTH, HEIGHT, "MIDNIGHT ENGINE" };
		EngineDevice engineDevice{ aveng_window };
		CameraManager cameraManager;
		ModelLibrary modelLib_;
		SceneFacade sceneFacade_;
		procgen::ChunkManager2 chunkManager_;
		TerrainController terrain_;
		Renderer renderer;

		std::unique_ptr<GameInput> gameInput_;
#ifdef ENABLE_EDITOR
		// Prefer pointers/optional to avoid duplicating big member-init lists
		std::unique_ptr<Editor> editor_;
		std::unique_ptr<EditorInput> editorInput_;
		std::unique_ptr<EditorGameRouter> inputRouter_; // Used when toggling between game/editor modes
#endif

		//
		std::unique_ptr<AvengFrame>  frame_;

		// ---- Internal APIs

		DebugController debug_;
		GameServices gameServices_;
		MidnightTextureSystem textureSystem_; // Injected into modelLibrary - owned/composed by Midnight

		// Model source (how assets load) lifetime owned by Midnight, passed into Renderer
		std::unique_ptr<IAssetSource> assetSource_;

		//
		std::unique_ptr<InputSystem> inputSystem_;
		
		/*
		* Note to self:
		* Stack allocate when 
		*	Object is trivial
			Object has no optional lifetime - probably the biggest factor due to the many implications
			Object has no rebuild path
			Object does not depend on editor/game mode
			Object does not touch GPU resources
		*/

	};

}