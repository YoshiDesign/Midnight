#pragma once
#include "CoreVK/VkRenderData.h"
#include "Utils/Timer.h"
#include "Utils/glm_includes.h"
#include "Core/Renderer/FramePacketBuilder.h"
#include "Game/data.h"

namespace aveng {
	class Renderer;
	class Editor;
	class EngineDevice;
	class ModelLibrary;
	struct VkRenderData;
	struct IRenderSceneView;
	struct IModelLibrary;

	class AvengFrame {

	public:
		AvengFrame(
			Renderer& renderer,
			ModelLibrary& modelLibrary,
			IRenderSceneView& sceneView,
			VkRenderData& renderData,
			GameData& gameData,
			EngineDevice& engineDevice,
			Editor* editor = nullptr);

		bool start_frame();
		void end_frame(const FramePacket& pkt, int currentFrameIndex);
		FramePacket& frame_packet(float deltaTime, int currentFrameIndex);
		bool render(float deltaTime);
		int currentFrameIndex();
		void reset_timers();

#ifdef ENABLE_EDITOR
		// Editor helpers
		bool hasEditorSelection();
#endif

	private:
		ModelLibrary& modelLib__;
		IRenderSceneView& sceneView_;
		FramePacketBuilder framePacketBuilder_;

		Timer mFrameTimer;
		Timer mFramePacketTimer;

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