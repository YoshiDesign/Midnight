#pragma once
#include <cassert>
#include "GUI/aveng_imgui.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Game/data.h"
#include "System/Camera/aveng_camera.h"
#include "System/Peripheral/KeyboardController.h"
#include "System/Input/EventPayloads.h"
#include "EditorData.h"

/**
* Note: At the moment, this class shares the currentFrameIndex held by the renderer.
* This can be decoupled if this class owns all of its own descriptor sets, buffers and resources (which it currently does!).
* It will still, however, rely on the correct swapchain (renderPass target) image index.
*/

namespace aveng {

	class SwapChain;
	class EngineDevice;
	class AvengWindow;
	class Renderer;

	class Editor {
	public:
		Editor(VkRenderData& _renderData, Renderer& _renderer, GameData& _gameData, EngineDevice& _engineDevice, AvengWindow& window, ModelAndInstanceData& modelInstanceData);
		~Editor();
		void init(SwapChain* swapchain);
		void render(unsigned int frameIndex, float frameTime);
		void cleanup();
		void readPixelDataPos();
		void updateData(float frameTime);

		bool hasSelection() { return editorData.eMousePick; }

		void setupSelectionHighlight(float dt);
		void setSelectedInstance();
		bool drawInstanceGizmo();
		void drawSelectedModels(int frameIndex);
		void updateCamera(float frameTime);
		float getAspectRatio();

		void beginGUICommands(int frameIdx);
		void endGUICommands(int frameIdx);
		void endGUIRenderPass(VkCommandBuffer commandBuffer);
		VkCommandBuffer getCurrentCommandBufferGUI() { return renderData.rdGUICommandBuffers[currentFrameIndex]; }

		void startGame();

		const glm::vec2 mouseXY() { return { editorData.eMouseXPos, editorData.eMouseYPos }; }

		bool createDescriptorLayouts();
		bool createDescriptorSets();
		bool createCommandBuffers();
		bool createPipelineLayouts();
		bool createSSBOs();
		void updateDescriptorSets();

		void endSwapChainLineRenderPass(VkCommandBuffer commandBuffer);

		void handleMouseClick(const MouseButtonEvent& e);
		void handleMouseMove(const MouseMoveEvent& e);

		void updateStorageBuffers();
		VkCommandBuffer getCurrentCommandBufferLines() const;

	private:

		Timer mUploadToVBOTimer{};
		unsigned int currentFrameIndex = 0; // Updated at render() from the renderer
		AvengCamera editor_camera{};
		float aspect;

		/* color hightlight for selection etc */
		std::vector<glm::vec2> mSelectedInstance{};
		std::vector<VkShaderStorageBufferData> mSelectedInstanceBuffers{};
		VkVertexBufferData mLineVertexBuffer{};

		bool mHighlightSelectedInstance = false;
		float mSelectedInstanceHighlightValue = 1.0f;
		CoordArrowsModel mCoordArrowsModel{};
		RotationArrowsModel mRotationArrowsModel{};
		ScaleArrowsModel mScaleArrowsModel{};
		VkLineMesh mCoordArrowsMesh{};
		unsigned int mCoordArrowsLineIndexCount = 0;
		std::shared_ptr<VkLineMesh> mLineMesh = nullptr;
		instanceEditMode rdInstanceEditMode = instanceEditMode::move;

		VkResult result;
		VkRenderData& renderData;
		GameData& gameData;
		ModelAndInstanceData& mModelInstanceData;
		EngineDevice& engineDevice;
		AvengWindow& window;
		Renderer& renderer;
		EditorData editorData;
		AvengAppObject editorViewerObject{ AvengAppObject::createAppObject(1001) };
		KeyboardController keyboardController{ editorViewerObject, gameData };
		AvengImgui aveng_imgui{ renderData, gameData, editorData, window, engineDevice, mModelInstanceData };
	};

}