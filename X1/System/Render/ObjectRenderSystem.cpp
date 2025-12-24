#include "ObjectRenderSystem.h"
#include "Core/aveng_window.h"
#include "Utils/window_callbacks.h"
#include "Game/data.h"

namespace xone {

	ObjectRenderSystem::ObjectRenderSystem() {}

	ObjectRenderSystem::~ObjectRenderSystem() {};

	void ObjectRenderSystem::initialize() 
	{
		// Invariant - We need at least one camera to be present (TODO - this could be bad)
		auto player_camera = std::make_unique<aveng::PlayerCamera>();
		player_camera_id = midnight.registerCamera("player_camera", std::move(player_camera)); // Editor will take over if enabled
		midnight.setActiveCamera(player_camera_id); 

		/*
		* TODO - Camera switching is still beta 12/23/25
		*/
	}

	void ObjectRenderSystem::render(float frameTime)
	{
#ifdef ENABLE_EDITOR
		if (midnight.mode() == aveng::AppMode::Game) {
			if (midnight.activeCameraId() != player_camera_id) {
				std::cout << "Setting Game as Active Camera..." << std::endl;
				midnight.setActiveCamera(player_camera_id);
			}
		}
#endif

		midnight.updateCamera(frameTime);
		midnight.render(frameTime);

	}

	void ObjectRenderSystem::loadGame(const std::string& scenePath)
	{
		std::cout << "Loading scene from: " << scenePath << std::endl;

	}

} 