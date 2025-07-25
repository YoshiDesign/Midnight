#include "avpch.h"
#include "XOne.h"
#include "Game/Math/aveng_math.h"
#include "Core/data.h"
#include "Core/aveng_frame_content.h"
#include "Utils/window_callbacks.h"
#include "Game/Player/GameplayFunctions.h"

namespace aveng {

	// Dynamic Helpers on window callback keys
	int WindowCallbacks::current_pipeline{ 1 };
	glm::vec3 WindowCallbacks::modRot{ 0.0f, 0.0f, 0.0f };
	glm::vec3 WindowCallbacks::modTrans{ 0.0f, 0.0f, 0.0f };
	int WindowCallbacks::posNeg = 1;
	bool WindowCallbacks::flightMode = false;
	float WindowCallbacks::modPI = PI;

	XOne::XOne()
	{
		loadAppObjects();
		objectRenderSystem.initialize();
		setupLights();
	}

	void XOne::run()
	{

		// Currently only used for clear color
		FrameContent frame_content = { glm::vec3{1, 1, 1}, glm::vec3{0.01f, 0.01f, 0.01f} };

		// Set callback functions for keys bound to the window
		glfwSetKeyCallback(aveng_window.getGLFWwindow(), WindowCallbacks::testKeyCallback);

		//camera.setViewTarget(glm::vec3(-1.f, -2.f, -20.f), glm::vec3(0.f, 0.f, 3.5f));

		auto currentTime = std::chrono::high_resolution_clock::now();

		// Render Loop
		while (!aveng_window.shouldClose()) {

			// Potentially blocking
			glfwPollEvents();

			// Calculate time between iterations
			auto newTime = std::chrono::high_resolution_clock::now();
			frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
			currentTime = newTime;

			objectRenderSystem.render(frameTime, frame_content);

		}

		// Block until all GPU operations quit.
		vkDeviceWaitIdle(engineDevice.device());
	}

	void XOne::setupLights()
	{
		// Clear any existing lights first
		objectRenderSystem.clearLights();

		// Add 5 diverse point lights to showcase the multiple light system

		for (int i = 1; i < 4; i++) {
			for (int j = 1; j < 5; j++) {

				objectRenderSystem.addLight(
					glm::vec3(-100 + i * -10.f, -2.f, 33 + j * 4.0f),  // position
					glm::vec3( 1.f, 0.0f, 0.0f),    // red color
					0.75f,                       
					i * j * 0.1f                           // slightly larger radius
				);

			}
		}
		
		// Test point lights - small radius, high intensity
		objectRenderSystem.addLight(
			glm::vec3(20.3, -16, 7.8f), // position
			glm::vec3(0.8f, 1.0f, 0.3f),     // yellow-green
			2.0f,                             // high intensity for point light
			0.15f                             // small radius = tight falloff
		);
		objectRenderSystem.addLight(
			glm::vec3(22.2, -16, 7.8f), // position
			glm::vec3(0.2f, 1.0f, 0.8f),     // cyan
			2.0f,                             // high intensity for point light  
			0.15f                             // small radius = tight falloff
		);

		// Add a few more point lights for testing
		objectRenderSystem.addLight(
			glm::vec3(-26.0f, -16.0f, 8.0f), // position
			glm::vec3(.0f, 0.4f, 0.8f),     // red-orange
			2.0f,                             // very high intensity
			0.5f                              // very small radius = very tight
		);
		
		objectRenderSystem.addLight(
			glm::vec3(-18.0f, -250.0f, 8.0f), // position
			glm::vec3(0.3f, 0.2f, 1.0f),      // blue
			2.0f,                              // medium-high intensity
			0.5f                               // slightly larger radius
		);

		objectRenderSystem.addLight(
			glm::vec3(18.0f, -250.0f, 0.0f), // position
			glm::vec3(0.3f, 0.2f, 1.0f),      // blue
			2.5f,                              // medium-high intensity
			1.0f                               // slightly larger radius
		);

		objectRenderSystem.addLight(
			glm::vec3(18.0f, -250.0f, -60.0f), // position
			glm::vec3(0.0f, 0.7f, 1.0f),      // blue
			2.5f,                              // medium-high intensity
			0.5f                               // slightly larger radius
		);

		objectRenderSystem.addLight(
			glm::vec3(18, -250.0f, -96.0f), // position
			glm::vec3(0.0f, 0.6f, 1.0f),      // blue
			2.5f,                              // medium-high intensity
			0.5f                               // slightly larger radius
		);

		objectRenderSystem.addLight(
			glm::vec3(44, -250.0f, -142.0f), // position
			glm::vec3(0.0f, 0.6f, 1.0f),      // blue
			2.5f,                              // medium-high intensity
			0.5f                               // slightly larger radius
		);

		objectRenderSystem.addLight(
			glm::vec3(89, -250.0f, -133.0f), // position
			glm::vec3(0.3f, 0.2f, 1.0f),      // blue
			2.5f,                              // medium-high intensity
			0.5f                               // slightly larger radius
		);

		objectRenderSystem.addLight(
			glm::vec3(92.4f, -250.0f, -93.0f), // position
			glm::vec3(0.3f, 0.2f, 1.0f),      // blue
			2.5f,                              // medium-high intensity
			0.5f                               // slightly larger radius
		);

		objectRenderSystem.addLight(
			glm::vec3(92.4f, -250.0f, -41.0f), // position
			glm::vec3(0.3f, 0.2f, 1.0f),      // blue
			2.5f,                              // medium-high intensity
			0.5f                               // slightly larger radius
		);
		std::cout << "Added " << objectRenderSystem.getLightCount() << " lights to the scene!" << std::endl;
	}

	/*
	*
	*/
	void XOne::loadAppObjects()
	{
		// Use the new scene loader system
		objectRenderSystem.loadGame("scenes/demo-scene.json");
	}

	//void XOne::pendulum(EngineDevice& engineDevice, int _max_rows)
	//{

	//	std::vector<float> factors;
	//	float length;
	//	float time = 7.0f;
	//	float gravity = 3.45f;
	//	float k = 7.0f;
	//	int max_rows = _max_rows;
	//	int row_modifier = 0;

	//	//std::unique_ptr<AvengModel> coloredCubeModel = AvengModel::createModelFromFile(engineDevice, "3D/colored_cube.obj");

	//	for (size_t i = 0; i < max_rows; i++)
	//	{
	//		//row_modifier = row_modifier % static_cast<int>(ceil(max_rows / 2) + 1);
	//		for (size_t j = 0; j < 1; j++) {
	//			auto gameObj = AvengAppObject::createAppObject(1000);
	//			//gameObj.model = coloredCubeModel;
	//			gameObj.model = AvengModel::createModelFromFile(engineDevice, "3D/colored_cube.obj");
	//			gameObj.meta.type = SCENE;

	//			if (i >= std::floor(max_rows / 2))
	//				gameObj.visual.pendulum_row = max_rows - row_modifier;
	//			else
	//				gameObj.visual.pendulum_row = row_modifier;

	//			length = gravity * glm::pow((time / (2 * glm::pi<float>()) * (k + gameObj.visual.pendulum_row + 1)), 2);
	//			length = length * .003;

	//			gameObj.visual.pendulum_delta = 0.0f;
	//			// To make this an actual pendulum, make the extent constant across all objects
	//			gameObj.visual.pendulum_extent = 70;
	//			gameObj.transform.velocity.x = length;
	//			gameObj.transform.translation = { 0.0f, static_cast<float>((i * -1.0f)), 0.0f };
	//			gameObj.transform.scale = { .4f, 0.4f, 0.4f };

	//			appObjects.emplace(gameObj.getId(), std::move(gameObj));
	//		}
	//		row_modifier++;
	//	}

	//}

}