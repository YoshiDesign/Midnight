#include "ObjectRenderSystem.h"
#include "Utils/window_callbacks.h"

namespace aveng {

	ObjectRenderSystem::ObjectRenderSystem(AvengWindow& window)
		: window{ window }
	{

#if ENABLE_EDITOR
		editor.init(renderer.pGetSwapChain());
#endif

		firstFrame = true;

		// Initial camera position
		viewerObject.transform.translation.z = -5.5f;
		viewerObject.transform.translation.y = -2.5f;
	}

	ObjectRenderSystem::~ObjectRenderSystem() {
#if ENABLE_EDITOR
		editor.cleanup();
#endif
	};

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
		// Check for queued models
		renderer.processPendingModelLoads();
		
		//
		updateCamera(frameTime);

		// TODO
		updateData(frameTime);

		// TODO - This is still being used to deliver view/proj data
		//renderer.updateCamera(player_camera.getProjection(), player_camera.getView());
		renderer.updateCamera();

		renderer.draw(frameTime);

#if ENABLE_EDITOR
		editor.render(renderer.getFrameIndex(), frameTime);
#endif
		// Render lights -- BINDS DESCRIPTORS -- BINDS A DIFFERENT PIPELINE
		// renderer.renderLights();
	
		// Update the current frame index
		renderer.endFrame();
	}

	void ObjectRenderSystem::updateCamera(float frameTime)
	{
		if (renderData.camera == 2) {
			// Fetched all the way from downtown (the swapchain)
			aspect = getAspectRatio();

			// Track key press to transform viewer object
			keyboardController.moveCameraXZ(window.getGLFWwindow(), frameTime);

			// Apply new viewer obj values to the camera
			player_camera.setViewYXZ(viewerObject.transform.translation + glm::vec3(0.f, 0.f, -.80f), viewerObject.transform.rotation);

			// Recalculate perspective
			player_camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);

			renderData.cameraProxy.projection = player_camera.getProjection();
			renderData.cameraProxy.view = player_camera.getView();
		}
	}

	/**
	* Just another POD struct for the UI/Editor to read from
	*/
	void ObjectRenderSystem::updateData(float frameTime)
	{

		game_data.num_objs = 2;
		game_data.cur_pipe = WindowCallbacks::getCurPipeline();
		game_data.dt = frameTime;
		game_data.camera_modPI = viewerObject.transform.modPI;

		game_data.cameraView = player_camera.getCameraView();
		game_data.cameraPos = viewerObject.transform.translation;
		game_data.cameraRot = viewerObject.transform.rotation;
		game_data.fly_mode = WindowCallbacks::flightMode;

	}

	void ObjectRenderSystem::loadGame(const std::string& scenePath)
	{
		std::cout << "Loading scene from: " << scenePath << std::endl;
		
		//renderer.loadScenes(scenePath.c_str());
	}

} //