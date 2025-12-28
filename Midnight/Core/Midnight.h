#pragma once
#include "Core/Input/InputSystem.h"
#include "Core/aveng_window.h"
#include "CoreVK/EngineDevice.h"
#include "Core/Renderer/ModelLibrary.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Renderer/AvengFrame.h"
#include "Runtime/World/InstanceManager.h"
#include "Core/Modeling/Sources/FilesystemModelSource.h"
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

		void initialize();
		void initializeDependencies();

		void beginFrameInput() { inputSystem_->beginFrame(); }

		const InputState& inputState() { return inputSystem_->inputState(); }

		// Editor Only
		const AppMode mode() { return game_data.currentAppMode; }
		// Editor Only

		std::unique_ptr<IModelSource> createModelSource();

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

		void registerInstanceManagerCallbacks();

#ifdef ENABLE_EDITOR
		void updateGUI(const InputState& state);
		const EditorData&	editorData() const	{ return editor_->data(); }
		EditorData&			editorData()		{ return editor_->data(); }
#endif
	private:
		// ---- External state
		GameData& game_data;

		// ---- Core state (order matters!)
		float aspect = 0.0f;
		CameraManager cameraManager;

		// Window must exist before EngineDevice (surface/device)
		AvengWindow  aveng_window{ WIDTH, HEIGHT, "MIDNIGHT ENGINE" };
		EngineDevice engineDevice{ aveng_window };

		// Render data depends on device/window typically
		VkRenderData renderData{};

		// Model source (how assets load) lifetime owned by Midnight, passed into Renderer
		std::unique_ptr<IModelSource> modelSource_;

		ModelLibrary modelLib_;

		// Renderer consumes device/window/renderData/cameraManager/modelSource
		Renderer renderer;

		// Instance managers depend on `renderer`
		InstanceManager<StaticTag>   staticMgr;
		InstanceManager<AnimatedTag> animMgr;

		// Input
		GameInput gameInput;

#ifdef ENABLE_EDITOR
		// Prefer pointers/optional to avoid duplicating big member-init lists
		std::unique_ptr<Editor> editor_;
		std::unique_ptr<EditorInput> editorInput_;
		std::unique_ptr<EditorGameRouter> inputRouter_;
#endif

		std::unique_ptr<InputSystem> inputSystem_;
		std::unique_ptr<AvengFrame>  frame_;

		// std::shared_ptr<AvengFrame> someFrame // do not do this elsewhere!!

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