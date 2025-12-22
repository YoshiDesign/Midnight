#include "Game.h"

namespace aveng {
	Game::Game(GameData& gameData) : gameData { gameData }
	{
	}

	Game::~Game() {}
	void Game::update(const InputState& state, float dt)
	{
	}

}