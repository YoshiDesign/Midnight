#pragma once
#include "Core/Input/EventPayloads.h"
#include "Core/Input/InputState.h"
#include "Game/data.h"

namespace aveng {

	class Game {
	public:
		Game(GameData& gameData);
		~Game();

		void update(const InputState& state, float frameTime);

	private:
		GameData& gameData;

	};
}