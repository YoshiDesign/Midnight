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

	/*
	*
	*/
	void XOne::loadAppObjects()
	{

		// objectRenderSystem.create3DObject("3D/ship.obj", THEME_4); // Model and Texture
		std::cout << "Pretending to load stuff... " << std::endl;
		auto ship = AvengAppObject::createAppObject(THEME_4);
		ship.model = AvengModel::createModelFromFile(engineDevice, "3D/ship.obj");
		ship.transform.translation = { 0.f, -10.f, 0.f };
		ship.transform.rotation = {0.f, 0.f, 180.0f};
		// appObjects.emplace(ship.getId(), std::move(ship));

		//auto ship2 = AvengAppObject::createAppObject(GRID);
		//ship2.model = AvengModel::createModelFromFile(engineDevice, "3D/canyon1.obj");
		//ship2.transform.translation = { 0.f, 0, 0.f };
		//ship2.transform.rotation = { 0.f, 0.f, 0.f };
		//appObjects.emplace(ship2.getId(), std::move(ship2));


		/*auto terrain = AvengAppObject::createAppObject(GRID);
		terrain.model = AvengModel::createModelFromFile(engineDevice, "3D/LargeTerrain2.obj");
		terrain.transform.translation = { 0.f, 0.f, 0.f };
		appObjects.emplace(terrain.getId(), std::move(terrain));*/

		//objectRenderSystem.setNumObjects(appObjects.size());
		objectRenderSystem.setNumObjects(2);

		// AvengModel::drawTriangle(engineDevice, { 1.0f, 1.0f, 1.0f });

		//for (size_t i = 0; i < 1; i++)
		//{

		//	for (size_t j = 0; j < 1; j++)
		//	{

		//		auto grid = AvengAppObject::createAppObject(THEME_2);
		//		grid.meta.type = GROUND;
		//		grid.model = AvengModel::createModelFromFile(engineDevice, "3D/plane.obj");
		//		grid.transform.translation = { 136.0f * i, -.1f, 0.0f};
		//		appObjects.emplace(grid.getId(), std::move(grid));

				//auto grid2 = AvengAppObject::createAppObject(THEME_1);
				//grid2.meta.type = GROUND;
				//grid2.model = AvengModel::createModelFromFile(engineDevice, "3D/plane.obj");
				//grid2.transform.translation = { 150.0f, -.1f, 170.0f };
				//appObjects.emplace(grid2.getId(), std::move(grid2));

		//	}

		//}

		//for (size_t i = 0; i < 10; i++) {
		//	for (size_t j = 0; j < 15; j++) {
		//		for (size_t k = 0; k < 5; k++) {

		//			auto triangle = AvengAppObject::createAppObject(NO_TEXTURE);
		//			triangle.meta.type = SCENE;
		//			triangle.model = AvengModel::drawTriangle(engineDevice, {static_cast<float>(i), static_cast<float>(j) * -1.0f, static_cast<float>(k) });
		//			appObjects.push_back(std::move(triangle));

		//		}
		//	}
		//}

		/*for (size_t i = 0; i < 10; i++)
		{

			for (size_t j = 0; j < 50; j++)
			{

				for (size_t k = 0; k < 4; k++) {
					auto sphere = AvengAppObject::createAppObject(NO_TEXTURE);
					sphere.meta.type = SCENE;
					sphere.model = AvengModel::createModelFromFile(engineDevice, "3D/sphere.obj");
					sphere.transform.translation = { static_cast<float>(i) * 1.5f, static_cast<float>(j) * -1.0f, static_cast<float>(k) * 2.0f };
					sphere.transform.scale = {0.1f, 0.1f, 0.1f};
					appObjects.emplace(sphere.getId(), std::move(sphere));

				}

			}

		}*/

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