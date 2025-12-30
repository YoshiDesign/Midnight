#pragma once
#include <cassert>
#include "Editor/API/IEditorUIAPI.h"
#include "Core/PointLightSystem.h"
#include "Core/Camera/aveng_camera.h"
#include "Core/Input/EventPayloads.h"
#include "Game/Camera/CameraManager.h"
#include "GUI/aveng_imgui.h"
#include "EditorCamera.h"
#include "EditorData.h"
#include "Game/data.h"

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
	class InputState;
	class SceneFacade;
	class IModelQuery;
	class IModelAnimQuery;
	class IInstanceQuery;

	class Editor : public IEditorUIAPI {

	/*
	* The Editor is a(n):
	* 1. Policy layer
	*	- Decides when something is allowed (e.g. model not loaded yet -> don't spawn)
	*	- Decides what to do when UI asks for something
	* 2. Adapter layer
	*	- Delegates getters -> const IInstanceQuery&
	*	- Delegates setters -> SceneFacade&
	* 3. UI API provider
	*	- Implements IEditorUIAPI and injects it into AvengImgui
	*/

	public:
		Editor(
			VkRenderData& _renderData,
			Renderer& _renderer,
			GameData& _gameData,
			EngineDevice& _engineDevice,
			AvengWindow& window,
			CameraManager& _cameraManager,
			SceneFacade& _sceneFacade,		// Use the scene_ member to utilize any operation that isn't a getter of scene data. (getOrLoadModel, unloadModel, spaw, spawnMany, destroyInstance, setTransform, etc.)
			const IModelQuery& modelQ,		// Getters of model metadata via ModelRegistry as injected from ModelLibrary
			const IModelAnimQuery& animQ,	// Getters of animation metadata via ModelRegistry as injected from ModelLibrary
			const IInstanceQuery& instQ		// Getters of SceneFacade strictly on behalf of instance metadata and lists as injected by SceneFacade
		);
		~Editor();


		void initialize(SwapChain* swapchain);
		void update(float frameTime, unsigned int frameIndex);
		void renderGUI(float frameTime);
		void updateLights();
		void drawModels(int frameIndex);
		void cleanup();
		void destroyTrash();
		void recreateFrameBuffers(SwapChain* swapchain);
		void initializePointLights();
		const EditorData& data() const	{ return editorData; }
		EditorData& data()				{ return editorData; }

		// void onModeSwitched(int frameIndex, AppMode& mode);

		bool hasSelection() { return editorData.eHasSelection; }
		bool hasClicked() { return editorData.eMousePick; }
		void debug() {
			/* Open to suggestions */
		}

		void readPixelDataPos();
		void setupSelectionHighlight(float dt);
		void setSelectedInstance();
		bool drawInstanceGizmo();
		float getAspectRatio();

		void beginGUICommands(int frameIdx);
		void endGUICommands(int frameIdx);
		void endGUIRenderPass(VkCommandBuffer commandBuffer);
		VkCommandBuffer getCurrentCommandBufferGUI() { return renderData.rdGUICommandBuffers.at(currentFrameIndex); }

		void startGame();

		const glm::vec2 mouseXY() { return { editorData.eMouseXPos, editorData.eMouseYPos }; }

		bool createDescriptorLayouts();
		bool createDescriptorSets();
		bool createCommandBuffers();
		bool createPipelineLayouts();
		bool createSSBOs();
		void updateDescriptorSets(int frameIndex);

		void endSwapChainLineRenderPass(VkCommandBuffer commandBuffer);

		void handleMouseClick(const MouseButtonEvent& e);
		void handleMouseMove(const MouseMoveEvent& e);

		void updateStorageBuffers();
		VkCommandBuffer getCurrentCommandBufferLines() const;

		void updateInputState(const InputState& state);

		template<class Tag>
		void updateSelectionForPool(
			aveng::InstanceManager<Tag>& mgr,
			const std::vector<InstanceHandle<Tag>>& drawOrder,
			std::vector<glm::vec2>& out,
			const AnyInstanceHandle& selectedAny,
			bool highlight,
			float blinkValue);



	private:

		CameraManager& cameraManager;
		int editor_camera_id;

		Timer mUploadToVBOTimer{};
		unsigned int currentFrameIndex = 0; // Updated at render() from the renderer
		float aspect;

		// GC stuff
		std::vector<PendingBufferDestroy> buffer_trash;

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
		// TODO : Make these const& ?
		EngineDevice& engineDevice;
		AvengWindow& window;
		Renderer& renderer;

		// Model & Instance Ops
		SceneFacade& scene_;			// Set Scene Data
		const IInstanceQuery& instQ_;	// Query Instances. From sceneFacade_.instanceQuery()
		const IModelQuery& modelQ_;		// Query Models.	From modelLib.query()
		const IModelAnimQuery& animQ_;	// Query Animation Data. From modelLib.animQuery()

		EditorData editorData;
		AvengImgui aveng_imgui{ renderData, gameData, editorData, window, engineDevice, scene_ };
		PointLightSystem pointLightSystem{ engineDevice, renderData };	// Light stuff
	};

}