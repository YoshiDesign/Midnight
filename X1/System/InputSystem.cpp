#include "System/InputSystem.h"

namespace aveng {

	InputSystem::~InputSystem() {}

	void InputSystem::handleMouseMove(double x, double y) {

		mouseDX = x - mouseX;
		mouseDY = y - mouseY;

		mouseX = x;
		mouseY = y;

		MouseMoveEvent e{ x, y, mouseDX, mouseDY, mouseButtons[GLFW_MOUSE_BUTTON_RIGHT] };

		handler.onMouseMove(e);

	}

	void InputSystem::handleMouseButton(int button, int action, int mods, double x, double y) {

		MouseButtonEvent e{button, action, mods, x, y};

		handler.onMouseButton(e);

	}

	void InputSystem::handleKey(int key, int scancode, int action, int mods)
	{
		KeyEvent e{ key, scancode, action, mods };

		handler.onKey(e);

	}

}