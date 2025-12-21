#include "ObjectRenderSystem.h"
#include "Core/aveng_window.h"
#include "Utils/window_callbacks.h"

namespace aveng {

	ObjectRenderSystem::ObjectRenderSystem(AvengWindow& window)
		: window{ window }
		, gameInput{holyShip}
#ifdef ENABLE_EDITOR
		, editorInput{&editor}
		, inputRouter{ mode_, editorInput, gameInput }
		, inputSystem{ inputRouter }
#else
		, inputSystem{ gameInput }
#endif
	{

		window.setInputSystem(&inputSystem);
		renderer.initialize(); // Very new
#if ENABLE_EDITOR
		editor.initialize(renderer.pGetSwapChain());
#endif

		firstFrame = true;

		// Initial camera position
		viewerObject.transform.translation.z = -5.5f;
		viewerObject.transform.translation.y = -2.5f;
	}

	ObjectRenderSystem::~ObjectRenderSystem() {	};

	void ObjectRenderSystem::initialize() 
	{

		// Lights, temporary placement
		//for (int i = 0; i <= 4; i++) {
		//	for (int j = 0; j < 5; j++) {
		//		
		//		renderer.addLight(
		//			glm::vec3(i * 4.f, -50.f, j * -4.0f),  // position
		//			glm::vec3(1.f, 0.0f, 0.0f),    // red color
		//			0.75f,						   // intensity
		//			i * 0.15f + 0.3f                 // radius
		//		);


		//		renderer.addLight(
		//			glm::vec3(30 + (i * 4.f), -50.f, 30 + (j * -4.0f)),  // position
		//			glm::vec3(0.8f, 0.9f, 0.8f),    // turquoise color
		//			0.75f,						   // intensity
		//			i * 0.15f + 0.3f                 // radius
		//		);

		//		renderer.addLight(
		//			glm::vec3(-30 + (i * 4.f), -50.f, 30 + (j * -4.0f)),  // position
		//			glm::vec3(1.f, 0.0f, 1.0f),    // Purple color
		//			0.75f,						   // intensity
		//			i * 0.15f + 0.3f                 // radius
		//		);


		//	}

		//	renderer.addLight(
		//		glm::vec3(-(i * 25.f), -25.f, -5.f),  // position
		//		glm::vec3(.75f, 0.9f, 1.0f),    // turquoise color
		//		0.75f,							// intensity
		//		0.5f                            // radius
		//	);

		//}
		
	}

	void ObjectRenderSystem::render(float frameTime)
	{

		// updateCamera(frameTime);

		// TODO? 
		// updateData(frameTime);
		if (mode_ == AppMode::Game) {
			// Update game state
			updateCamera(frameTime);
			holyShip.update(frameTime);
		}

		frame.render(frameTime);

		// Render lights -- BINDS DESCRIPTORS -- BINDS A DIFFERENT PIPELINE
		// renderer.renderLights();

	}

	// This is the game's camera updates, not the editor's. It's more convenient to do this here at the moment
	void ObjectRenderSystem::updateCamera(float frameTime)
	{

		// Fetched all the way from downtown (the swapchain)
		aspect = getAspectRatio();

		// Track key press to transform viewer object
		keyboardController.moveCameraXZ(window.getGLFWwindow(), frameTime);

		// Apply new viewer obj values to the camera
		player_camera.setViewYXZ(viewerObject.transform.translation + glm::vec3(0.f, 0.f, -.80f), viewerObject.transform.rotation);

		// Recalculate perspective
		player_camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);

		// Update the renderer's camera data
		renderData.cameraProxy.projection = player_camera.getProjection();
		renderData.cameraProxy.view = player_camera.getView();

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
		
		//renderer.loadScenes(scenePath.c_str());
	}

} //