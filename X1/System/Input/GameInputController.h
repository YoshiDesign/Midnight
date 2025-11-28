#pragma once
#include "IInputController.h"
#include "EventPayloads.h"
#include "Game/Game.h"

namespace aveng {

	class GameInputController : public IInputController {
	public:
		GameInputController();
		explicit GameInputController(Game& game) : mGame(game) {}

		void onMouseMove(const MouseMoveEvent& e);
		void onMouseButton(const MouseButtonEvent& e);
		void onKey(const KeyEvent& e);

		void update(float dt);  // use the dx/dy accumulated

	private:
		Game& mGame;
	};

}