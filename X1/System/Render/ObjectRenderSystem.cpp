#include "ObjectRenderSystem.h"
#include "Core/aveng_window.h"
#include "Utils/window_callbacks.h"

namespace aveng {

	ObjectRenderSystem::ObjectRenderSystem() {}

	ObjectRenderSystem::~ObjectRenderSystem() {};

	void ObjectRenderSystem::initialize() 
	{
		auto player_camera = std::make_unique<PlayerCamera>();
		player_camera_id = midnight.registerCamera("player_camera", std::move(player_camera));
		std::cout << "player_camera_id\t" << player_camera_id << std::endl;
		midnight.setActiveCamera(player_camera_id); // Invariant - We need at least one camera to be present (TODO - bad)
	}

	void ObjectRenderSystem::render(float frameTime)
	{
		midnight.render(frameTime);

		midnight.beginFrameInput();
		midnight.updateCamera(frameTime);

#ifdef ENABLE_EDITOR
		if (midnight.mode() == AppMode::Game) {
			if (midnight.activeCameraId() != player_camera_id) {
				std::cout << "Setting Game as Active Camera..." << std::endl;
				midnight.setActiveCamera(player_camera_id);
			}
#endif
			holyShip.update(midnight.inputState(), frameTime);

#ifdef ENABLE_EDITOR
		}
#endif

	}

	void ObjectRenderSystem::loadGame(const std::string& scenePath)
	{
		std::cout << "Loading scene from: " << scenePath << std::endl;

	}

} 