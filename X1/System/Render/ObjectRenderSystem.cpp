#include "ObjectRenderSystem.h"
#include "Core/aveng_window.h"
#include "Utils/window_callbacks.h"

namespace aveng {

	ObjectRenderSystem::ObjectRenderSystem()
	{
		// Initial camera position
		viewerObject.transform.translation.z = -5.5f;
		viewerObject.transform.translation.y = -2.5f;
	}

	ObjectRenderSystem::~ObjectRenderSystem() {	};

	void ObjectRenderSystem::initialize() 
	{

	}

	void ObjectRenderSystem::render(float frameTime)
	{
		midnight.beginFrameInput();
		midnight.render(frameTime);

		if (midnight.mode() == AppMode::Game) { // Editor only
			// Update game state
			updateCamera(frameTime);
			holyShip.update(midnight.inputState(), frameTime);
		} // Editor only

	}

	// This is the game's camera updates, not the editor's. It's more convenient to do this here at the moment
	void ObjectRenderSystem::updateCamera(float frameTime)
	{

		// Fetched all the way from downtown (the swapchain)
		aspect = getAspectRatio();

		// Track key press to transform viewer object
		// keyboardController.moveCameraXZ(window.getGLFWwindow(), frameTime);

		// Apply new viewer obj values to the camera
		player_camera.setViewYXZ(viewerObject.transform.translation + glm::vec3(0.f, 0.f, -.80f), viewerObject.transform.rotation);

		// Recalculate perspective
		player_camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);

		midnight.updateCamera(player_camera.getProjection(), player_camera.getView());

	}

	/**
	* Just another POD struct for the UI/Editor to read from
	*/
	void ObjectRenderSystem::updateData(float frameTime)
	{

		gameData.num_objs = 2;
		gameData.cur_pipe = WindowCallbacks::getCurPipeline();
		gameData.dt = frameTime;
		gameData.camera_modPI = viewerObject.transform.modPI;

		gameData.cameraView = player_camera.getCameraView();
		gameData.cameraPos = viewerObject.transform.translation;
		gameData.cameraRot = viewerObject.transform.rotation;
		gameData.fly_mode = WindowCallbacks::flightMode;

	}

	void ObjectRenderSystem::loadGame(const std::string& scenePath)
	{
		std::cout << "Loading scene from: " << scenePath << std::endl;

	}

} 