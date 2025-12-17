#pragma once
#include "System/Interface/IInputHandler.h"
#include "Game/Game.h"
#include "Game/data.h"
#include "Editor.h"

namespace aveng {
	
	class InputSystem {
	public:
		// Accepts an interface for input handling
		explicit InputSystem(IInputHandler& _handler) : handler(_handler) {}
		InputSystem();
		~InputSystem();

		void handleMouseMove(double x, double y);
		void handleMouseButton(int button, int action, int mods, double x, double y);
		void handleKey(int key, int scancode, int action, int mods);

		bool setInputSystem() {}

	private:
		AppMode mode{ AppMode::Editor };

		double mouseX = 0;
		double mouseY = 0;
		double mouseDX = 0;
		double mouseDY = 0;

		IInputHandler& handler;

		bool mouseButtons[GLFW_MOUSE_BUTTON_LAST + 1]{};

	};

}