#pragma once
#include "GUI/aveng_imgui.h"
#include "CoreVK/swapchain.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/LinePipeline.h"
#include "Core/Renderer/Renderer.h"
#include "Core/aveng_window.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/data.h"
#include "System/Camera/aveng_camera.h"
#include "System/Peripheral/KeyboardController.h"
#include "EditorData.h"

/**
* Note: At the moment, this class shares the currentFrameIndex held bythe renderer.
* This can be decoupled if this class owns all of its own descriptor sets, buffers and resources.
*/


namespace aveng {

	class Editor {
	public:
		Editor(VkRenderData& _renderData, Renderer& _renderer, GameData& _gameData, EngineDevice& _engineDevice, AvengWindow& window, ModelAndInstanceData& modelInstanceData);
		~Editor();
		void init(SwapChain* swapchain);
		void render(unsigned int frameIndex, float frameTime);
		void cleanup();
		void waitFrames();

		void setupSelectionHighlight(float dt);
		void setSelectedInstance();
		bool drawInstanceGizmo();
		void drawSelectedModels();
		void updateCamera(float frameTime);
		float getAspectRatio() { return renderer.getAspectRatio(); }
		void setCamera(int cam) { renderData.camera = cam; } // Tmp

		bool createDescriptorLayouts();
		bool createDescriptorSets();
		bool createCommandBuffers();
		bool createPipelineLayouts();
		bool createSSBOs();
		void updateDescriptorSets(int iters = 1);

		void updateStorageBuffers();

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

		instanceEditMode rdInstanceEditMode = instanceEditMode::move;

		std::shared_ptr<VkLineMesh> mLineMesh = nullptr;

		VkResult result;
		VkRenderData& renderData;
		GameData& gameData;
		ModelAndInstanceData& mModelInstanceData;
		EngineDevice& engineDevice;
		AvengWindow& window;
		Renderer& renderer;
		EditorData editorData;
		AvengAppObject editorViewerObject{ AvengAppObject::createAppObject(1000) };
		KeyboardController keyboardController{ editorViewerObject, gameData };
		AvengImgui aveng_imgui{ renderData, gameData, editorData, window, engineDevice, mModelInstanceData };
	};

}