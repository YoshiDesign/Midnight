#include "Game.h"
namespace aveng {
	Game::Game(GameData& gameData) : gameData { gameData }
	{
	}

	Game::~Game() {}
	void Game::handleMouseClick(const MouseButtonEvent& e) {
		
	}

	void Game::handleMouseMove(const MouseMoveEvent& e) {
		
	}

	void Game::update(float dt)
	{
	}

	void Game::startEditor()
	{
		gameData.currentAppMode = AppMode::Editor;
	}

}