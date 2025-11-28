#pragma once
#include "System/Input/EventPayloads.h"
#include "Game/data.h"

namespace aveng {

	class Game {
	public:
		Game(GameData& gameData);
		~Game();

		void handleMouseClick(const MouseButtonEvent& e);
		void handleMouseMove(const MouseMoveEvent& e);
		void update(float frameTime);
		void startEditor();

	private:
		GameData& gameData;

	};
}