#include "Core/Input/InputSystem.h"

namespace aveng {

	InputSystem::~InputSystem() {}

	void InputSystem::handleMouseMove(double x, double y) {

		MouseMoveEvent e{ x, y, state.mouseDown[GLFW_MOUSE_BUTTON_RIGHT] };
		updateInputState(e);
		handler.onMouseMove(e);

	}

	void InputSystem::handleMouseButton(int button, int action, int mods, double x, double y) {

		MouseButtonEvent e{button, action, mods, x, y};
		updateInputState(e);
		handler.onMouseButton(e);

	}

	void InputSystem::handleKey(int key, int scancode, int action, int mods) {

		KeyEvent e{ key, scancode, action, mods };
		updateInputState(e);
		handler.onKey(e);

	}

	void InputSystem::updateInputState(const MouseMoveEvent e)
	{
		// TODO
		//if ((e.x > window_width || e.x < 0) || (e.y > window_height || e.y < 0)) {
		//	return;
		//}

		state.mouseX = e.x;
		state.mouseY = e.y;

		// Previous mouse positions and deltas are recorded during state.beginFrame() based on these
	}
	void InputSystem::updateInputState(const MouseButtonEvent e)
	{

		if (e.action == GLFW_PRESS) {
			if (!state.mouseDown[e.button]) {
				state.mousePressed[e.button] = true;
			}
			state.mouseDown[e.button] = true;
		
		}
		else if (e.action == GLFW_RELEASE) {
			if (state.mouseDown[e.button]) {
				state.mouseReleased[e.button] = true;
			}
			state.mouseDown[e.button] = false;
		}

	}
	void InputSystem::updateInputState(const KeyEvent e)
	{
		
		if (e.key < 0 || e.key >= InputState::MaxKeys) {
			return;
		}

		if (e.action == GLFW_PRESS) {
			if (!state.keyDown[e.key]) {
				state.keyPressed[e.key] = true;
			}
			state.keyDown[e.key] = true;
		}

		else if (e.action == GLFW_RELEASE) {
			if (state.keyDown[e.key]) {
				state.keyReleased[e.key] = true;
			}
			state.keyDown[e.key] = false;
		}
		// GLFW_REPEAT intentionally ignored
	}

	// Just a quick method for the window to check on events directly from this class
	bool InputSystem::isKeyPressed(int key) {
		return state.keyPressed[key];
	}

	// Editor Only
	void InputSystem::setMode(const AppMode& _mode) {
		// Note that these are decoupled. Input handler can change modes without consequences. 
		// Rendering needs sync
		
		if (!gameData.modeSwitchRequested) {

			gameData.requestedMode = _mode;
			gameData.modeSwitchRequested = true;

			handler.setMode(_mode);
		}

	}

}