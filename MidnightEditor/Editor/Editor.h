#pragma once
#include "avpch.h"
#include "Core/PointLightSystem.h"
// #include "Core/Camera/aveng_camera.h"
#include "Core/Input/EventPayloads.h"
#include "Editor/API/IEditorUIAPI.h"
#include "Editor/API/SceneEditAPI.h"
#include "Editor/GUI/aveng_imgui.h"
// #include "EditorCamera.h"
#include "EditorData.h"
#include "Game/data.h"

/**
* Note: At the moment, this class shares the currentFrameIndex held by the renderer.
* This can be decoupled if this class owns all of its own descriptor sets, buffers and resources (which it currently does!).
* It will still, however, rely on the correct swapchain (renderPass target) image index.
*/

namespace aveng {

	class CameraManager;
	class SwapChain;
	class EngineDevice;
	class AvengWindow;
	class Renderer;
	class SceneFacade;
	struct InputState;
	struct IModelQuery;
	struct IModelAnimQuery;
	struct IInstanceQuery;
	struct FramePacket;

	class Editor {

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
			SceneFacade& _sceneFacade // Composition root - Editor doesn't need this, we just pass it to the SceneEditAPI in the init list
		);
		~Editor();

		void initialize(SwapChain* swapchain);
		void update(float frameTime, unsigned int frameIndex);
		void renderGUI(float frameTime);
		void renderLights();
		void drawModels(const IModelLibrary& modelLib, const FramePacket& pkt, int frameIndex);
		void renderTerrain();
		void cleanup();
		void destroyTrash();
		void recreateFrameBuffers(SwapChain* swapchain);

		const EditorData& data() const	{ return editorData; }
		EditorData& data()				{ return editorData; }

		// void onModeSwitched(int frameIndex, AppMode& mode);

		bool hasSelection() { return editorData.selectedMany.size() > 0; }
		bool hasClicked() { return editorData.eMousePick; }
		void debug() {
			/* Open to suggestions */
		}

		void readPixelDataPos(const FramePacket& pkt);
		bool drawInstanceGizmo();
		float getAspectRatio();

		void beginGUICommands(int frameIdx);
		void endGUICommands(int frameIdx);
		void endGUIRenderPass(VkCommandBuffer commandBuffer);
		VkCommandBuffer getCurrentCommandBufferGUI() { return renderData.rdGUICommandBuffers.at(currentFrameIndex); }

		void startGame();

		const glm::vec2 mouseXY() { return { editorData.eMouseXPos, editorData.eMouseYPos }; }

		bool createCommandBuffers();

		void endSwapChainLineRenderPass(VkCommandBuffer commandBuffer);

		void handleMouseClick(const MouseButtonEvent& e);
		void handleMouseMove(const MouseMoveEvent& e);

		void updateStorageBuffers();
		VkCommandBuffer getCurrentCommandBufferLines() const;

		void updateInputState(const InputState& state);

		const VkPipeline pointLightPipeline() { return pointLightSystem.getPipeline(); }

		//template<class Tag>
		//void updateSelectionForPool(
		//	aveng::InstanceManager<Tag>& mgr,
		//	const std::vector<InstanceHandle<Tag>>& drawOrder,
		//	std::vector<glm::vec2>& out,
		//	const AnyInstanceHandle& selectedAny,
		//	bool highlight,
		//	float blinkValue);

	private:

		CameraManager& cameraManager;
		int editor_camera_id;

		unsigned int currentFrameIndex = 0; // Updated at render() from the renderer
		float aspect;

		// GC stuff
		std::vector<PendingBufferDestroy> buffer_trash;

		/* color hightlight for selection etc */
		// std::vector<glm::vec2> mSelectedInstance{};
		// std::vector<VkShaderStorageBufferData> mSelectedInstanceBuffers{};
		VkVertexBufferData mLineVertexBuffer{};

		// bool mHighlightSelectedInstance = false;
		// float mSelectedInstanceHighlightValue = 1.0f;

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
		SceneEditAPI sceneEdit_;		// We pass the sceneFacade to the SceneEditAPI

		EditorData editorData;
		AvengImgui aveng_imgui;
		PointLightSystem pointLightSystem;	// Editor requires its own PLS because it uses its own renderpass
	};

}