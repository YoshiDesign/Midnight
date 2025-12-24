#pragma once
#include "Core/Input/IInputHandler.h"
#include "Core/Input/InputState.h"
#include "Game/data.h"
#include "Editor.h"

namespace aveng {
	
	class InputSystem {
	public:
		// Accepts an interface for input handling
		explicit InputSystem(IInputHandler& _handler, GameData& _gameData) : handler{ _handler }, gameData{_gameData} {}
		InputSystem();
		~InputSystem();

		void handleMouseMove(double x, double y);
		void handleMouseButton(int button, int action, int mods, double x, double y);
		void handleKey(int key, int scancode, int action, int mods);

		// Quick references. Use sparingly - I don't want these becoming hot. Totally fine for anything Editor related.
		bool isKeyPressed(int key);

		// Getter
		const InputState& inputState() const { return state; }

		// Setters
		void updateInputState(const MouseMoveEvent);
		void updateInputState(const MouseButtonEvent);
		void updateInputState(const KeyEvent);
		void setMode(const AppMode& mode);
		const AppMode& getMode() { return gameData.currentAppMode; }

		// Reset all values
		void beginFrame() { state.beginFrame(); }

	private:
		GameData& gameData;
		IInputHandler& handler;
		InputState state;

	};

}