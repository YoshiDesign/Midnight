#pragma once
#include "Core/Input/IInputHandler.h"
#include "Core/Input/InputState.h"
#include "Game/Game.h"
#include "Game/data.h"
#include "Editor.h"

namespace aveng {
	
	class InputSystem {
	public:
		// Accepts an interface for input handling
		explicit InputSystem(IInputHandler& _handler) : handler{ _handler } {}
		InputSystem();
		~InputSystem();

		void handleMouseMove(double x, double y);
		void handleMouseButton(int button, int action, int mods, double x, double y);
		void handleKey(int key, int scancode, int action, int mods);

		// Getter
		const InputState& inputState() const { return state; }

		// Setters
		void updateInputState(const MouseMoveEvent);
		void updateInputState(const MouseButtonEvent);
		void updateInputState(const KeyEvent);

		// Reset all values
		void beginFrame() { state.beginFrame(); }

	private:
		AppMode mode{ AppMode::Editor };

		IInputHandler& handler;
		InputState state;

	};

}