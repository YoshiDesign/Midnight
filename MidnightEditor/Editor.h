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
		void update(float frameTime);
		void renderGUI(unsigned int frameIndex, float frameTime);
		void updateCamera(float frameTime);
		void drawSelectedModels(int frameIndex);
		void cleanup();

		bool hasSelection() { return editorData.eHasSelection; }
		bool hasClicked() { return editorData.eMousePick; }
		void debug() {
			std::cout << "miSelectedEditorModel: " << mModelInstanceData.miSelectedEditorModel << std::endl;
			std::cout << "miSelectedEditorInstance: " << mModelInstanceData.miSelectedEditorInstance << std::endl;
			std::cout << "has selection: " << editorData.eHasSelection << std::endl;
			std::cout << "eHighlightSelectedInstance: " << editorData.eHighlightSelectedInstance << std::endl;
			std::cout << "eCurrentSelectedInstance: " << editorData.eCurrentSelectedInstance << std::endl;
			std::cout << "eSelectedInstance[EditorInstance].y: " << editorData.eSelectedInstance[mModelInstanceData.miSelectedEditorInstance].y << std::endl;
		}

		void readPixelDataPos();
		void setupSelectionHighlight(float dt);
		void setSelectedInstance();
		bool drawInstanceGizmo();
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
		void updateDescriptorSets(int set = 1000);

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

		// Gizmo Arrows
		CoordArrowsModel mCoordArrowsModel{};
		RotationArrowsModel mRotationArrowsModel{};
		ScaleArrowsModel mScaleArrowsModel{};
		// 
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