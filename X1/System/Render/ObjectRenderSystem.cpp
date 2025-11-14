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
		renderer.processPendingModelLoads();
		
		updateCamera(frameTime);

		// This needs to be updated to accommodate game data
		updateData(frameTime);

		// Update frame data in renderer
		renderer.updateFrameData(aveng_camera.getProjection(), aveng_camera.getView());

		frameIndex = renderer.draw(frameTime);
		if (frameIndex == WTF_BOOM)
		{
			throw std::runtime_error("fatal error");
		}

		editor.render(frameIndex);

		// Render lights -- BINDS DESCRIPTORS -- BINDS A DIFFERENT PIPELINE
		// renderer.renderLights();
	
		// renderer.endFrame();
	}

	void ObjectRenderSystem::updateCamera(float frameTime)
	{
		// Fetched all the way from downtown (the swapchain)
		aspect = getAspectRatio();

		// Track key press to transform viewer object
		keyboardController.moveCameraXZ(window.getGLFWwindow(), frameTime);

		// Apply new viewer obj values to the camera
		aveng_camera.setViewYXZ(viewerObject.transform.translation + glm::vec3(0.f, 0.f, -.80f), viewerObject.transform.rotation);

		// Recalculate perspective
		aveng_camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);
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

		game_data.cameraView = aveng_camera.getCameraView();
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