#pragma once
#include "Core/Midnight.h"
#include "Game/data.h"
#ifdef ENABLE_EDITOR
#include "Editor.h"
#endif
#include "avpch.h"

namespace xone {

	class ObjectRenderSystem {
	public:
		//ObjectRenderSystem();
		explicit ObjectRenderSystem();
		~ObjectRenderSystem();
		ObjectRenderSystem& operator=(const ObjectRenderSystem&) = delete;
		ObjectRenderSystem(const ObjectRenderSystem&) = delete;

		// Application-level interface
		void initialize();
		void loadGame(const std::string& scenePath);
		float getAspectRatio() { return midnight.getAspectRatio(); }

		// Light management
		void addLight(const glm::vec3& position, const glm::vec3& color, float intensity = 1.0f, float radius = 0.1f);

		// Main render function - simplified interface
		void render(float frameTime);

		// Application-specific updates
		void updateCamera(float frameTime, const aveng::InputState& state);

		VkDevice getEngineDevice() { return midnight.device(); }

		bool shouldClose() { return midnight.shouldClose(); }

		const aveng::InputState& inputState() { return midnight.inputState(); }
		void updateInputState() { midnight.beginFrameInput(); }

#ifdef ENABLE_EDITOR
		aveng::EditorData& editorData() { return midnight.editorData(); }
#endif

	private:

		int last_sec;
		float aspect;
		int frameIndex;
		int player_camera_id;

		// Game & State
		aveng::GameData gameData; // This is a big design flaw at the moment. Don't rely on it
		aveng::Midnight midnight{ gameData };

	};

}