#pragma once
#include "Core/Input/InputSystem.h"
#include "Core/aveng_window.h"
#include "Game/data.h"
#include "Editor.h"

namespace aveng {

	class Midnight {

	public:
		Midnight(Editor& editor, AvengWindow& window);
		~Midnight() = default;

		void beginFrameInput() { inputSystem.beginFrame(); }

		const InputState& inputState() { return inputSystem.inputState(); }
		const AppMode mode() { return mode_; }

	private:

#ifdef ENABLE_EDITOR
		AppMode mode_ = AppMode::Editor;
#else
		AppMode mode_ = AppMode::Game;
#endif

		GameInput gameInput;
		InputSystem inputSystem;
		EditorInput editorInput;
		EditorGameRouter inputRouter;

	};

}