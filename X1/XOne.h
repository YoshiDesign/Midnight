#pragma once
#include <vector>

#include "Core/Renderer/ObjectRenderSystem.h"
//#include "CoreVK/aveng_descriptors.h"
//#include "Core/Renderer/AvengImageSystem.h"
//#include "Core/Renderer/PointLightSystem.h"
#include "Core/Scene/app_object.h"
//#include "GUI/aveng_imgui.h"
#include "Core/aveng_window.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVk/aveng_buffer.h"

#include "Core/Peripheral/KeyboardController.h"

namespace aveng {

	class XOne {

	public:

		static constexpr int WIDTH = 800;
		static constexpr int HEIGHT = 600;

		XOne();
		~XOne() {};

		XOne(const XOne&) = delete;
		XOne& operator=(const XOne&) = delete;
		void run();

		void pendulum(EngineDevice& engineDevice, int _max_rows);

	private:

		void loadAppObjects();
		void updateCamera(float frameTime, AvengAppObject& viewerObject, KeyboardController& cameraController, AvengCamera& camera);
		void updateData();
		glm::vec3 clear_color = { 0.3f, 0.9f, 0.7f };

		/*
		* !! Order of member initialization matters !!
		* See: § 12.6.2 of the C++ Standard
		*/
		Data data;
		// The window API - Stack allocated
		AvengWindow aveng_window{ WIDTH, HEIGHT, "MIDNIGHT ENGINE" };
		AvengAppObject viewerObject{ AvengAppObject::createAppObject(1000) };
		EngineDevice engineDevice{ aveng_window };
		AvengAppObject::Map appObjects;
		AvengCamera camera{};
		ObjectRenderSystem objectRenderSystem{ engineDevice, viewerObject, aveng_window};
		KeyboardController keyboardController{ viewerObject, data };

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
