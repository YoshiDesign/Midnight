#pragma once
#include "Input/EditorInputController.h"
#include "Input/GameInputController.h"
#include "Game/Game.h"
#include "Game/data.h"
#include "Editor.h"

namespace aveng {
	
	class InputSystem {
	public:

		InputSystem();
#ifdef ENABLE_EDITOR
		InputSystem(Game& _game, Editor* _editor) : mGame{ _game }, mEditor{ _editor } {}
#endif
		InputSystem(Game& _game) : mGame{ _game } {}
		~InputSystem();

		void handleMouseMove(double x, double y);
		void handleMouseButton(int button, int action, int mods, double x, double y);
		void handleKey(int key, int scancode, int action, int mods);
		void setMode(AppMode _mode) { mode = _mode; }
		const AppMode getMode() const { return mode; }
		bool setInputSystem() {}

	private:
		AppMode mode{ AppMode::Editor };

		double mouseX = 0;
		double mouseY = 0;
		double mouseDX = 0;
		double mouseDY = 0;

		bool mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1]{};
#ifdef ENABLE_EDITOR
		Editor* mEditor = nullptr;
		EditorInputController editorInput{mEditor};
#endif

		Game& mGame;
		GameInputController gameInput{mGame};

	};

}