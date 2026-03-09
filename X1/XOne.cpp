#include "avpch.h"
#include "XOne.h"
#include "Runtime/Play/PlayManager.h"
#include "Game/Instance/HolyShip.h"
#include "Game/Instance/Starfield.h"
#ifdef ENABLE_EDITOR
#include "Editor/EditorData.h"
#endif
#include <Runtime/Play/GameRegistry.h>
#include <Runtime/Play/IGameInstance.h>

namespace xone {

	inline void RegisterGames(GameRegistry& registry, PlayManager& play) {

		/* Technically this is "register scene" */
		registry.registerGame("holyship", [&play]() {
			return std::make_unique<HolyShip>(play);
		});

		registry.registerGame("starfield", [&play]() {
			return std::make_unique<Starfield>(play);
		});
	}

	XOne::XOne() {}

	void XOne::run()
	{

		GameRegistry registry;
		PlayManager play(registry);

		// Temporary Placement
		/* We need one camera to be active at all times, this is technically a limitation */
		auto player_camera = std::make_unique<aveng::PlayerCamera>();
		player_camera_id = midnight.registerCamera("player_camera", std::move(player_camera));

		// Note: Editor's camera will take over if it's initialized
		midnight.setActiveCamera(player_camera_id);

		// "Yo, Tank..."
		RegisterGames(registry, play);

		std::chrono::time_point<std::chrono::steady_clock> loopStartTime = std::chrono::steady_clock::now();
		std::chrono::time_point<std::chrono::steady_clock> loopEndTime = std::chrono::steady_clock::now();

		// Render Loop
		while (!midnight.shouldClose()) {
			// Potentially blocking
			glfwPollEvents();

			// Clear all input states, calculate mouse & scroll deltas, reset key edges, etc.
			midnight.beginFrameInput();
			TickContext tick{ 
				frameTime, 
				0, // midnight.frameIndex(), 
				midnight.inputState() 
			};

			// We may not need to pass gameServices during update(), 
			// if the members of that struct don't necessarily update, or are just APIs
			play.update(tick, midnight.gameServices());

#ifdef ENABLE_EDITOR
			/* There currently exist no methods for switching cameras within the context of AppMode::Game
			   This is a hard switch from the window callbacks set by aveng_window */
			if (midnight.mode() == aveng::AppMode::Game) {
				if (midnight.activeCameraId() != player_camera_id) {
					std::cout << "Setting Game as Active Camera..." << std::endl;
					midnight.setActiveCamera(player_camera_id);
				}
			}
#endif

			midnight.updateCamera(frameTime);
			midnight.render(frameTime);

			// Calculate time between frames
			loopEndTime = std::chrono::steady_clock::now();
			frameTime = std::chrono::duration<float, std::chrono::seconds::period>(loopEndTime - loopStartTime).count();
			loopStartTime = loopEndTime;

#ifdef ENABLE_EDITOR
			// Process Editor Commands
			for (const auto cmd : editorData().drainCommands()) {
				switch (cmd.type) {
				case aveng::EditorCommand::Type::RequestPlay: play.requestPlay(cmd.payload); break;
				// case aveng::EditorCommand::Type::RequestStop: play.requestStop(); break;
				default: break;
				}
			}
#endif

		}

		midnight.shutdown();

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