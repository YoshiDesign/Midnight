#pragma once
#include "aveng/SystemContext.h"
#include "Core/Renderer/Renderer.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/aveng_buffer.h"
#include "CoreVK/GFXPipeline.h"
#include "Core/aveng_window.h"
#include "Core/aveng_model.h"
#include "System/Camera/aveng_camera.h"
#include "Game/app_object.h"
#include "Game/data.h"

namespace aveng {


	class SystemData {
	public:

		SystemData(
			EngineDevice& device, 
			AvengWindow& aveng_window, 
			AvengCamera& camera, 
			Renderer& renderer, 
			AvengAppObject::Map& appObjects, 
			GameData& gameData);

		~SystemData();
		
		SystemContext& systemContext();

	private:
		void createContext();

		EngineDevice& device;
		AvengWindow& aveng_window;
		AvengCamera& camera;
		Renderer& renderer;
		GameData& gameData;
		AvengAppObject::Map& appObjects;
		SystemContext context;
	};
}