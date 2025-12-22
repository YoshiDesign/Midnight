#include "avpch.h"
#include "XOne.h"
#include "Game/Math/aveng_math.h"
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

		objectRenderSystem.initialize();		
		// Testing 123
		// testAnimationSystem();
	}

	void XOne::run()
	{

		std::chrono::time_point<std::chrono::steady_clock> loopStartTime = std::chrono::steady_clock::now();
		std::chrono::time_point<std::chrono::steady_clock> loopEndTime = std::chrono::steady_clock::now();

		// Render Loop
		while (!objectRenderSystem.shouldClose()) {

			objectRenderSystem.render(frameTime);

			// Potentially blocking
			glfwPollEvents();

			// Calculate time between frames
			loopEndTime = std::chrono::steady_clock::now();
			frameTime = std::chrono::duration<float, std::chrono::seconds::period>(loopEndTime - loopStartTime).count();
			loopStartTime = loopEndTime;

		}

		// Block until all GPU operations quit.
		vkDeviceWaitIdle(objectRenderSystem.getEngineDevice());
	}

	/*
	* Test the new Assimp-based animation system
	*/
	

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