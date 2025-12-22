#include "Midnight.h"

namespace aveng {

	Midnight::Midnight(GameData& _gamedata) : game_data{ _gamedata }
	{
		aveng_window.setInputSystem(&inputSystem);
		renderer.initialize(); // Very new
#if ENABLE_EDITOR
		editor.initialize(renderer.pGetSwapChain());
#endif

	}

	void Midnight::updateCamera(glm::mat4 projection, glm::mat4 view) {
		// Update the renderer's camera data
		renderData.cameraProxy.projection = projection;
		renderData.cameraProxy.view = view;
	}
}