#include "System/InputSystem.h"

namespace aveng {

	InputSystem::~InputSystem() {}

	void InputSystem::handleMouseMove(double x, double y) {

		mouseDX = x - mouseX;
		mouseDY = y - mouseY;

		mouseX = x;
		mouseY = y;

		MouseMoveEvent e{ x, y, mouseDX, mouseDY, mouseButtons[GLFW_MOUSE_BUTTON_RIGHT] };

		if (mode == AppMode::Editor) {
			editorInput.onMouseMove(e);
		}
		else {
			gameInput.onMouseMove(e);
		}
	}

	void InputSystem::handleMouseButton(int button, int action, int mods, double x, double y) {

		MouseButtonEvent e{button, action, mods, x, y};

		if (mode == AppMode::Editor) {
			editorInput.onMouseButton(e);
		}
		else {
			gameInput.onMouseButton(e);
		}
	}

	void InputSystem::handleKey(int key, int scancode, int action, int mods)
	{
		KeyEvent e{ key, scancode, action, mods };

		if (mode == AppMode::Editor) {
			editorInput.onKey(e);
		}
		else {
			gameInput.onKey(e);
		}
		// glfwPollEvents will handle key input if AppMode::Game

	}

}