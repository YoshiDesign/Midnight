#pragma once

#include "Game/data.h"
#include "Core/Midnight.h"

#ifdef ENABLE_EDITOR
#include "Editor/Editor.h"
#endif

namespace xone {

	class XOne {

	public:

		XOne();
		~XOne() {};

		XOne(const XOne&) = delete;
		XOne& operator=(const XOne&) = delete;
		void run();

		VkDevice getEngineDevice() { return midnight.device(); }

		bool shouldClose() { return midnight.shouldClose(); }

		const aveng::InputState& inputState() { return midnight.inputState(); }
		// void updateInputState() {  }


#ifdef ENABLE_EDITOR
		aveng::EditorData& editorData() { return midnight.editorData(); }
#endif
		// void pendulum(EngineDevice& engineDevice, int _max_rows);

	private:

		float frameTime;
		int player_camera_id;
		::aveng::GameData gameData; // This is a big design flaw at the moment. Don't rely on it
		::aveng::Midnight midnight{ gameData };

	};

}

/*
* Old notes from like 2021
	Things to keep in mind:
	Object Space - Objects initially exist at the origin of object space
	World Space  - The model matrix created by the <>'s transform component, coordinates objects with World Space
	Camera Space - The view transformation, applied to our objects, moves objects from World Space into the camera's perspective,
				   where the camera is at the origin and all object's coord's are relative to their position and orientation

			* The camera does not actually exist, we're just transforming objects AS IF the camera were there

		We then apply the projection matrix, capturing whatever is contained by the viewing frustrum, which then transforms
		it to the canonical view volume. As a final step the viewport transformation maps this region to actual pixel values.

*/
