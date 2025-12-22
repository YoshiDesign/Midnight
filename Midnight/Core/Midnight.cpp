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

	void Midnight::render(float frameTime) {

#ifdef ENABLE_EDITOR
		if (game_data.modeSwitchRequested)
		{
			std::cout << "Detected Mode Switch to: " << static_cast<int>(game_data.requestedMode) << std::endl;
			// optional but often wise:
			// renderer.waitForCurrentFrameFence();  // ensure we aren't still using old frame resources
			// or vkDeviceWaitIdle(device);          // heavy hammer but great for debugging

			game_data.currentAppMode = game_data.requestedMode;
			game_data.modeSwitchRequested = false;

			// Good to know - In case anything needs dealing with
			// editor.onModeSwitched(frame.currentFrameIndex(), game_data.currentAppMode);
		}
#endif
		frame.render(frameTime); 
	}

	void Midnight::updateCamera(glm::mat4 projection, glm::mat4 view) {
		// Update the renderer's camera data
		renderData.cameraProxy.projection = projection;
		renderData.cameraProxy.view = view;
	}

}