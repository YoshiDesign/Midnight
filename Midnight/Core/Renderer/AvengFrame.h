#pragma once
#include "CoreVK/VkRenderData.h"
#include "Utils/glm_includes.h"
#include "Game/data.h"

namespace aveng {
	class Renderer;
	class Editor;
	class EngineDevice;
	struct VkRenderData;
	struct IRenderSceneView;
	struct IModelLibrary;

	class AvengFrame {

	public:
		AvengFrame(
			Renderer& renderer,
			const IRenderSceneView& sceneView,
			const IModelLibrary& modelLib, // used to get Model pointers to the renderer, *for now*
			VkRenderData& renderData,
			GameData& gameData,
			EngineDevice& engineDevice,
			Editor* editor = nullptr);

		bool render(float deltaTime);
		int currentFrameIndex();

#ifdef ENABLE_EDITOR
		// Editor helpers
		bool hasEditorSelection();
#endif

	private:
		const IModelLibrary& modelLib_;
		const IRenderSceneView& sceneView_;

		std::vector<VkCommandBuffer> commandBuffers;
		bool drawGizmo = false;
		VkResult result;
		EngineDevice& engineDevice;
		VkRenderData& renderData;
		GameData& gameData;
		Renderer& renderer;
		Editor* pEditor; // fwd declare
	};
}