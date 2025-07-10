#pragma once
#include <vector>
#include "aveng/SystemContext.h"
#include "Game/data.h"
#include "System/Render/ObjectRenderSystem.h"
#include "Game/app_object.h"
#include "Core/aveng_window.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVk/aveng_buffer.h"
#include "System/Peripheral/KeyboardController.h"
#include "Utils/SystemData.h"

namespace aveng {

	class XOne {

	public:

		static constexpr int WIDTH = 2080;
		static constexpr int HEIGHT = 960;

		XOne();
		~XOne() {};

		XOne(const XOne&) = delete;
		XOne& operator=(const XOne&) = delete;
		void run();

		SystemContext& context() { return systemData.systemContext(); };

		void pendulum(EngineDevice& engineDevice, int _max_rows);

	private:

		void loadAppObjects();
		void updateCamera(float frameTime, AvengAppObject& viewerObject, KeyboardController& cameraController, AvengCamera& camera);
		void updateData();

		/*
		* !! Order of member initialization matters !!
		* See: § 12.6.2 of the C++ Standard
		*/
		GameData data;
		// The window API - Stack allocated
		AvengWindow aveng_window{ WIDTH, HEIGHT, "MIDNIGHT ENGINE" };
		AvengAppObject viewerObject{ AvengAppObject::createAppObject(1000) };
		EngineDevice engineDevice{ aveng_window };
		AvengAppObject::Map appObjects;
		AvengCamera camera{};
		ObjectRenderSystem objectRenderSystem{ engineDevice, viewerObject, aveng_window};
		KeyboardController keyboardController{ viewerObject, data };
		SystemData systemData{ engineDevice, aveng_window, camera, objectRenderSystem.pRenderer(), appObjects, data};

		float aspect;
		float frameTime;

	};

}

/*
	Things to keep in mind:
	Object Space - Objects initially exist at the origin of object space
	World Space  - The model matrix created by the AppObject's transform component coordinates objects with World Space
	Camera Space - The view transformation, applied to our objects, moves objects from World Space into the camera's perspective,
				   where the camera is at the origin and all object's coord's are relative to their position and orientation

			* The camera does not actually exist, we're just transforming objects AS IF the camera were there

		We then apply the projection matrix, capturing whatever is contained by the viewing frustrum, which then transforms
		it to the canonical view volume. As a final step the viewport transformation maps this region to actual pixel values.

*/
