#pragma once
#include "CoreVK/VkRenderData.h"
#include "Utils/glm_includes.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Game/data.h"

namespace aveng {
	class Renderer;
	class Editor;
	class EngineDevice;

	class AvengFrame {

	public:
		AvengFrame(Renderer& renderer,
			VkRenderData& renderData,
			GameData& gameData,
			EngineDevice& engineDevice,
			ModelAndInstanceData& modelInstanceData,
			Editor* editor = nullptr);

		bool render(float deltaTime);

#ifdef ENABLE_EDITOR
		// Editor helpers
		bool hasEditorSelection();
#endif

	private:
		std::vector<VkCommandBuffer> commandBuffers;
		bool drawGizmo = false;
		VkResult result;
		ModelAndInstanceData& modelInstanceData;
		EngineDevice& engineDevice;
		VkRenderData& renderData;
		GameData& gameData;
		Renderer& renderer;
		Editor* pEditor;
	};
}