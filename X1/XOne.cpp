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
		objectRenderSystem.descriptorSetup();
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
		
		// 1. Bright red light - positioned to the left
		objectRenderSystem.addLight(
			glm::vec3(-8.0f, -5.0f, 2.0f),  // position
			glm::vec3(1.0f, 0.2f, 0.2f),   // red color
			2.0f,                           // high intensity
			0.15f                           // slightly larger radius
		);

		// 2. Cool blue light - positioned to the right
		objectRenderSystem.addLight(
			glm::vec3(8.0f, -3.0f, -1.0f), // position
			glm::vec3(0.2f, 0.4f, 1.0f),   // blue color
			1.5f,                           // medium intensity
			0.12f                           // medium radius
		);

		// 3. Vibrant green light - positioned above
		objectRenderSystem.addLight(
			glm::vec3(0.0f, -15.0f, 5.0f), // position
			glm::vec3(0.2f, 1.0f, 0.3f),   // green color
			1.8f,                           // high intensity
			0.1f                            // standard radius
		);

		// 4. Warm white light - positioned as main lighting
		objectRenderSystem.addLight(
			glm::vec3(2.0f, -8.0f, 8.0f),  // position
			glm::vec3(1.0f, 0.9f, 0.7f),   // warm white
			3.0f,                           // very high intensity
			0.2f                            // larger radius for main light
		);

		// 5. Purple accent light - positioned for dramatic effect
		objectRenderSystem.addLight(
			glm::vec3(-3.0f, -12.0f, -4.0f), // position
			glm::vec3(0.8f, 0.2f, 1.0f),     // purple/magenta
			1.2f,                             // lower intensity for accent
			0.08f                             // smaller radius
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