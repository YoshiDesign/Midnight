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
		updateGUI(inputState());
		if (game_data.modeSwitchRequested)
		{
			game_data.currentAppMode = game_data.requestedMode;
			game_data.modeSwitchRequested = false;

			// Good to know - In case anything needs dealing with
			// editor.onModeSwitched(frame.currentFrameIndex(), game_data.currentAppMode);
		}
#endif
		frame.render(frameTime); 
	}

	// This is the game's camera updates, not the editor's. It's more convenient to do this here at the moment
	void Midnight::updateCamera(float frameTime)
	{
		// Fetched all the way from downtown (the swapchain)
		aspect = renderer.getAspectRatio();

		// Track key press to transform viewer object
		// keyboardController.moveCameraXZ(window.getGLFWwindow(), frameTime);
		cameraManager.update(frameTime, inputState());

		// Apply new viewer obj values to the camera
		cameraManager.active().camera.setViewYXZ(
			cameraManager.active().camera.transform().translation + glm::vec3(0.f, 0.f, -.80f),
			cameraManager.active().camera.transform().rotation);

		// Recalculate perspective
		cameraManager.active().camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);

		// Punt to the renderer
		renderData.cameraProxy.projection = cameraManager.active().camera.getProjection();
		renderData.cameraProxy.view = cameraManager.active().camera.getView();

	}

	int Midnight::registerCamera(std::string name, std::unique_ptr<ICameraDriver> cameraDriver) {
		return cameraManager.createCamera(std::move(name), std::move(cameraDriver));
	}

	void Midnight::updateGUI(const InputState& state) {
		editor.updateInputState(state);
	}

}