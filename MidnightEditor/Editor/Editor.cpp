
#include "Editor.h"
#include "Editor/Utils/selection_utils.h"
#include "Utils/Logger.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/swapchain.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/TerrainPipeline.h"
#include "CoreVK/LinePipeline.h"
#include "CoreVK/PipelineLayout.h"
#include "Core/aveng_window.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Input/InputState.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
// #include "Runtime/World/InstanceManager.h"
#include "Runtime/Facade/SceneFacade.h"
#include "Core/Renderer/FramePacketBuilder.h"
#include "Game/Camera/CameraManager.h"
#include "Editor/EditorCamera.h"
#include "Runtime/Play/Controller/TerrainController.h"

namespace aveng {

	Editor::Editor(
		VkRenderData& _renderData, 
		Renderer& _renderer, 
		GameData& _gameData, 
		EngineDevice& _engineDevice, 
		AvengWindow& window, 
		CameraManager& _cameraManager,
		SceneFacade& _sceneFacade
		)
		: 
		 renderData{ _renderData }
		, renderer{ _renderer }
		, gameData{ _gameData }
		, engineDevice{ _engineDevice }
		, window{ window }
		, cameraManager{ _cameraManager }
		, sceneEdit_{ _sceneFacade } // Composition root
		, aveng_imgui{ renderData, sceneEdit_, editorData, window, engineDevice} 
		, pointLightSystem{ engineDevice, renderData }
	{

		// Register a camera
		auto editor_camera = std::make_unique<EditorCamera>();
		editor_camera_id = cameraManager.createCamera("editor_camera", std::move(editor_camera));
		std::cout << "editor_camera_id\t" << editor_camera_id << std::endl;

		//renderData.rdLineDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		//renderData.rdAvengSelectionDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		//renderData.rdAvengAnimationSelectionDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		/* valid, but emtpy */
		mLineMesh = std::make_shared<VkLineMesh>();
		// Logger::log(1, "%s: line mesh storage initialized\n", __FUNCTION__);

		// Editor GUI command buffers
		if (!createCommandBuffers())
		{
			throw std::runtime_error("editor failed to create command buffers");
		}

	}

	Editor::~Editor() 
	{
		cleanup();
	}

	float Editor::getAspectRatio() {
		return renderer.getAspectRatio();
	}

	void Editor::startGame()
	{
		// gameData.currentAppMode = AppMode::Game;
	}

	void Editor::renderGUI(float frameTime)
	{
		// Note: This needs to occur after update() to sync the frame index
		// This also needs to occur within a valid renderpass
		aveng_imgui.newFrame();
		aveng_imgui.runGUI();
		aveng_imgui.render(currentFrameIndex);

	}

	void Editor::update(float frameTime, unsigned int frameIndex) {

		currentFrameIndex = frameIndex;

		editorData.cameraTransform = cameraManager.active().transform;

		// Update our camera data
		editorData.cameraDebugList.clear();
		editorData.cameraDebugList.reserve(cameraManager.cameraCount());
		cameraManager.forEachCamera([&](const auto& cam) {
			editorData.cameraDebugList.push_back({ cam.name, cam.transform, cam.active });
		});

		if (cameraManager.activeId() != editor_camera_id) {
			std::cout << "Setting Editor as Active Camera..." << std::endl;
			cameraManager.setActive(editor_camera_id);
		}

		// setupSelectionHighlight(frameTime);
		// setSelectedInstance();
		// updateStorageBuffers();
		renderer.updateBufferViews();
	}

	void Editor::beginGUICommands(int frameIdx)
	{

		if (!engineDevice.resetCommandBuffer(renderData.rdGUICommandBuffers[frameIdx], 0)) {
			std::printf("%s error: failed to reset command buffer\n", __FUNCTION__);
			throw std::runtime_error("Failed to begin Graphics Command Buffer 1");
		}

		if (!engineDevice.beginSingleShotCommand(renderData.rdGUICommandBuffers[frameIdx])) {
			std::printf("%s error: failed to begin command buffer\n", __FUNCTION__);
			throw std::runtime_error("Failed to begin Graphics Command Buffer 0");
		}

	}

	void Editor::endGUICommands(int frameIdx)
	{
		VkResult result = vkEndCommandBuffer(renderData.rdGUICommandBuffers[frameIdx]);
		if (result != VK_SUCCESS) {
			std::printf("%s error: could not end render pass (error: %i)\n", __FUNCTION__, result);
			throw std::runtime_error("error: could not end render pass");
		}
	}

	void Editor::endGUIRenderPass(VkCommandBuffer commandBuffer)
	{
		// assert(isFrameStarted && "Can't call endSwapChain if frame is not in progress.");
		assert(commandBuffer == getCurrentCommandBufferGUI() &&
			"[End GUI RenderPass] Can't end render pass on command buffer from a different frame");
		vkCmdEndRenderPass(commandBuffer);
	}

	//void Editor::initializePointLights()
	//{

	//	pointLightSystem.initialize(renderData.rdSelectionRenderpass, 1, false);
	//	std::cout << "(Editor) PointLightSystem initialized" << std::endl;

	//}

	void Editor::readPixelDataPos(const FramePacket& pkt)
	{
		if (!editorData.eMousePick || renderer.isRecreatingSwapChain()) {
			return;
		}

		std::cout << "Reading Pixel Data" << std::endl;

		// vkQueueWaitIdle(engineDevice.graphicsQueue());

		/* VALIDATION: Check coordinates are non-negative */
		if (editorData.eMouseXPos < 0 || editorData.eMouseYPos < 0) {
			Logger::log(1, "%s: Invalid negative coordinates (%d, %d)\n", 
				__FUNCTION__, editorData.eMouseXPos, editorData.eMouseYPos);
			editorData.eMousePick = false;
			return;
		}

		/* VALIDATION: Check coordinates are within viewport bounds */
		uint32_t viewportWidth = renderer.pGetSwapChain()->width();
		uint32_t viewportHeight = renderer.pGetSwapChain()->height();
		if (static_cast<unsigned int>(editorData.eMouseXPos) >= viewportWidth ||
			static_cast<unsigned int>(editorData.eMouseYPos) >= viewportHeight) {
			Logger::log(1, "%s: Coordinates out of bounds (%d, %d), viewport is (%u x %u)\n",
				__FUNCTION__, editorData.eMouseXPos, editorData.eMouseYPos, viewportWidth, viewportHeight);
			editorData.eMousePick = false;
			return;
		}

		int pickId = renderer.getPixelValueFromPos(editorData.eMouseXPos, editorData.eMouseYPos);

		if (pickId >= 0) {

			renderData.selectedPickId = static_cast<int>(pickId);

			editorData.primarySelection = pkt.pickToHandle[pickId];
			// editorData.primarySelection = pickId;
			editorData.selectedMany.clear();
			editorData.eShowTRSPanel = true; // Not necessary here, just being explicit

			// For Multi-Select
			addUnique(editorData.selectedMany, editorData.primarySelection);

			// std::cout << "Picked Handle Index: " << editorData.primarySelection.index;
		}
		else {
			std::cout << "False Selection: SelectedID\t" << pickId << std::endl;
			std::cout << "Deselecting instance:\t" << renderData.selectedPickId << std::endl;
			renderData.selectedPickId = 0;

			editorData.primarySelection = AnyInstanceHandle{};
			// editorData.primarySelection = -1;
			editorData.selectedMany.clear();
			editorData.eShowTRSPanel = false;
		}

		editorData.eMousePick = false;
		
	}

	void Editor::recreateFrameBuffers(SwapChain* swapchain)
	{
		swapchain->createEditorSelectionFramebuffers();
	}

	// TODO - Make this synonymous to the renderer's initialization.
	void Editor::initialize(SwapChain* swapchain) 
	{
		// The primary renderpass, just in case
		// VkRenderPass renderPass = swapchain->getRenderPass();

		// Create the secondary renderpass used by the line rendering pipeline (for Gizmos)
		if (!swapchain->createSecondaryRenderpass(renderData.rdLineRenderpass))
		{
			Logger::log(1, "%s error; could not create secondary renderpass\n", __FUNCTION__);
			throw std::runtime_error("noob");
		}

		// Create the selection renderpass used for selection highlighting
		if (!swapchain->createSelectionRenderpass(renderData.rdSelectionRenderpass))
		{
			Logger::log(1, "%s error; could not create selection renderpass\n", __FUNCTION__);
			throw std::runtime_error("editor fail 0");
		}

		// Create the selection renderpass used for selection highlighting
		if (!swapchain->createSecondaryRenderpass(renderData.rdImguiRenderpass))
		{
			Logger::log(1, "%s error; could not create selection renderpass\n", __FUNCTION__);
			throw std::runtime_error("editor fail 0");
		}

		std::cout << "Creating Framebuffers from Editor " << std::endl;

		swapchain->createEditorSelectionFramebuffers();
		// createPipelineLayouts();

		// Debug - Static
		std::string vertexShaderFile = "shaders/debug.vert.spv";
		std::string fragmentShaderFile = "shaders/debug.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdAvengBindlessPipelineLayout,
			renderData.rdDebugPipeline, renderData.rdSelectionRenderpass, 2, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 0");
		}

		// Debug - Animated
		vertexShaderFile = "shaders/debug_skinning.vert.spv";
		fragmentShaderFile = "shaders/debug_skinning.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdAvengBindlessPipelineLayout,
			renderData.rdDebugAnimatedPipeline, renderData.rdSelectionRenderpass, 2, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 0");
		}

		vertexShaderFile = "shaders/line.vert.spv";
		fragmentShaderFile = "shaders/line.frag.spv";
		if (!LinePipeline::init(engineDevice, renderData.rdLineRenderpass, renderData.rdAvengBindlessPipelineLayout,
			renderData.rdLinePipeline, vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init Assimp line drawing shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 1");
		}

		vertexShaderFile = "shaders/aveng_selection.vert.spv";
		fragmentShaderFile = "shaders/aveng_selection.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdAvengBindlessPipelineLayout,
			renderData.rdAvengSelectionPipeline, renderData.rdSelectionRenderpass, 2,
			vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init aveng Selection shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 2");
		}

		vertexShaderFile = "shaders/aveng_skinning_selection.vert.spv";
		fragmentShaderFile = "shaders/aveng_skinning_selection.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdAvengBindlessPipelineLayout,
			renderData.rdAvengAnimationSelectionPipeline, renderData.rdSelectionRenderpass, 2,
			vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init aveng Skinning Selection shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 3");
		}

		// Terrain Debug Pipeline
		vertexShaderFile = "shaders/terrain_debug.vert.spv";
		fragmentShaderFile = "shaders/terrain_debug.frag.spv";
		if (!TerrainPipeline::init(engineDevice, renderData.rdAvengBasicTerrainPipelineLayout,
			renderData.rdAvengEditorBasicTerrainPipeline, renderData.rdSelectionRenderpass, 2, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Terrain Debug shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 3");
		}

		aveng_imgui.init(
			renderData.rdImguiRenderpass,
			swapchain->imageCount()
		);

		pointLightSystem.initialize(
			renderData.rdSelectionRenderpass, 
			2, true); 
	}

	void Editor::renderLights()
	{
		renderer.renderLights(pointLightSystem.getPipeline());
	}

	bool Editor::createCommandBuffers()
	{
		renderData.rdLineCommandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		if (!engineDevice.initCommandBuffers(renderData.rdLineCommandBuffers))
		{
			return false;
		}

		renderData.rdGUICommandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		if (!engineDevice.initCommandBuffers(renderData.rdGUICommandBuffers))
		{
			return false;
		}

		return true;
	}

	void Editor::handleMouseClick(const MouseButtonEvent& e) {
		aveng_imgui.handleMouseButtonEvents(e.button, e.action, e.mods);
	}

	void Editor::handleMouseMove(const MouseMoveEvent& e) {
		aveng_imgui.handleMousePositionEvents(e.x, e.y, e.rmbDown);
	}

	bool Editor::drawInstanceGizmo() {

		if (!hasSelection()) return false;

		/* draw coordinate lines */
		mCoordArrowsLineIndexCount = 0;
		mLineMesh->vertices.clear();
		/// TODO
		//if (mModelInstanceData.miSelectedEditorInstance > 0) { // Instance 0 is the null instance

		//	if (mModelInstanceData.miSelectedEditorInstance >= mModelInstanceData.miAssimpInstances.size()) {
		//		Logger::log(1, "%s[INVALID GIZMO INDEX]\n", __FUNCTION__);
		//		throw std::runtime_error("Bye");
		//	}

		//	InstanceSettings instSettings = mModelInstanceData.miAssimpInstances.at(mModelInstanceData.miSelectedEditorInstance)->getInstanceSettings();

		//	/* draw coordiante arrows at origin of selected instance */
		//	switch (renderData.rdInstanceEditMode) {
		//	case instanceEditMode::move:
		//		mCoordArrowsMesh = mCoordArrowsModel.getVertexData();
		//		break;
		//	case instanceEditMode::rotate:
		//		mCoordArrowsMesh = mRotationArrowsModel.getVertexData();
		//		break;
		//	case instanceEditMode::scale:
		//		mCoordArrowsMesh = mScaleArrowsModel.getVertexData();
		//		break;
		//	}

		//	mCoordArrowsLineIndexCount += mCoordArrowsMesh.vertices.size();

		//	// Set to the selected model's position/origin
		//	std::for_each(mCoordArrowsMesh.vertices.begin(), mCoordArrowsMesh.vertices.end(),
		//		[=](auto& n) {
		//		n.color /= 2.0f;
		//		n.position = glm::quat(glm::radians(instSettings.isWorldRotation)) * n.position;
		//		n.position += instSettings.isWorldPosition;
		//	});

		//	mLineMesh->vertices.insert(mLineMesh->vertices.end(),
		//		mCoordArrowsMesh.vertices.begin(), mCoordArrowsMesh.vertices.end());
		//}

		if (mCoordArrowsLineIndexCount > 0) {

			if (!engineDevice.resetCommandBuffer(renderData.rdLineCommandBuffers.at(currentFrameIndex), 0)) {
				Logger::log(1, "%s error: failed to reset line drawing command buffer\n", __FUNCTION__);
				return false;
			}

			if (!engineDevice.beginSingleShotCommand(renderData.rdLineCommandBuffers.at(currentFrameIndex))) {
				Logger::log(1, "%s error: failed to begin line drawing command buffer\n", __FUNCTION__);
				return false;
			}

			// Begin - We can safely piggy back on the renderer's beginSwapChainRenderPass method - it's polymorphic
			renderer.beginSwapChainRenderPass(
				renderData.rdLineCommandBuffers.at(currentFrameIndex),
				renderer.getCurrentFramebuffer(),
				renderData.rdLineRenderpass);

			// TODO - Timers
			VertexBuffer::uploadData(engineDevice, mLineVertexBuffer, *mLineMesh);

			vkCmdBindPipeline(renderData.rdLineCommandBuffers.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdLinePipeline);

			// NOTE: The correct descriptor set should have been the last set to be bound by this point.
			//vkCmdBindDescriptorSets(renderData.rdLineCommandBuffers.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_GRAPHICS,
			//	renderData.rdLinePipelineLayout, 0, 1, &renderData.rdLineDescriptorSets.at(currentFrameIndex), 0, nullptr);

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(renderData.rdLineCommandBuffers.at(currentFrameIndex), 0, 1, &mLineVertexBuffer.buffer, &offset);
			vkCmdSetLineWidth(renderData.rdLineCommandBuffers.at(currentFrameIndex), 3.0f);
			vkCmdDraw(renderData.rdLineCommandBuffers.at(currentFrameIndex), static_cast<uint32_t>(mLineMesh->vertices.size()), 1, 0, 0);

			// Fin - Specific end for Line renderpasses
			endSwapChainLineRenderPass(renderData.rdLineCommandBuffers.at(currentFrameIndex));

			if (!engineDevice.endCommandBuffer(renderData.rdLineCommandBuffers.at(currentFrameIndex))) {
				Logger::log(1, "%s error: failed to end line drawing command buffer\n", __FUNCTION__);
				return false;
			}
			return true;
		}

		return false;

	}

	// Just use the returned values directly if working in renderer.cpp. This is for clients
	VkCommandBuffer Editor::getCurrentCommandBufferLines() const
	{
		//std::cout << "getCurrentCommandBufferLines: " << currentFrameIndex << std::endl;
		assert(renderer.isFrameInProgress() && "Cannot get command buffer. The frame is not in progress.");
		return renderData.rdLineCommandBuffers.at(currentFrameIndex);
	}

	void Editor::updateInputState(const InputState& state)
	{
		aveng_imgui.updateInputState(state);
	}

	void Editor::endSwapChainLineRenderPass(VkCommandBuffer commandBuffer)
	{
		assert(renderer.isFrameInProgress() && "Can't call endSwapChain if frame is not in progress.");
		assert(commandBuffer == getCurrentCommandBufferLines() &&
			"Can't end render pass on command buffer from a different frame");
		vkCmdEndRenderPass(commandBuffer);
	}

	void Editor::drawModels(const IModelLibrary& modelLib, const FramePacket& pkt, int frameIndex)
	{
		renderer.drawModelsBindless(
			pkt,
			modelLib,
			renderData.rdCommandBuffersGraphics[frameIndex],
			renderData.rdDebugPipeline,
			renderData.rdDebugAnimatedPipeline);
	}

	void Editor::renderTerrain() {
		renderer.terrainController().update();
		renderer.terrainController().renderDebug(
			renderData.rdCommandBuffersGraphics.at(currentFrameIndex),
			renderData.rdAvengEditorBasicTerrainPipeline,
			currentFrameIndex
		);
	}

	void Editor::cleanup()
	{

		destroyTrash();

		// Free command buffers
		vkFreeCommandBuffers(
			engineDevice.device(),
			engineDevice.commandPoolGraphics(),
			static_cast<uint32_t>(renderData.rdLineCommandBuffers.size()),
			renderData.rdLineCommandBuffers.data()
		);
		renderData.rdLineCommandBuffers.clear();

		vkFreeCommandBuffers(
			engineDevice.device(),
			engineDevice.commandPoolGraphics(),
			static_cast<uint32_t>(renderData.rdGUICommandBuffers.size()),
			renderData.rdGUICommandBuffers.data()
		);
		renderData.rdGUICommandBuffers.clear();

		VertexBuffer::cleanup(engineDevice, mLineVertexBuffer);

		//for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			//vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdAvengSelectionDescriptorSets[i]);
			//vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdAvengAnimationSelectionDescriptorSets[i]);
			// vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdLineDescriptorSets[i]);
		//}
		
		//vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengSelectionDescriptorLayout, nullptr);
		//vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengAnimationSelectionDescriptorLayout, nullptr);
		// vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdLineDescriptorLayout, nullptr);
		//vkDestroyPipelineLayout(engineDevice.device(), renderData.rdDebugPipelineLayout, nullptr);
		//vkDestroyPipelineLayout(engineDevice.device(), renderData.rdDebugAnimatedPipelineLayout, nullptr);
		//vkDestroyPipelineLayout(engineDevice.device(), renderData.rdAvengSelectionPipelineLayout, nullptr);
		//vkDestroyPipelineLayout(engineDevice.device(), renderData.rdAvengAnimationSelectionPipelineLayout, nullptr);
		// vkDestroyPipelineLayout(engineDevice.device(), renderData.rdLinePipelineLayout, nullptr);

		// vkDestroyDescriptorPool(engineDevice.device(), renderData.editorDescriptorPool, nullptr);
	}

	/* Check the destruction queue for impending doom */
	void Editor::destroyTrash()
	{
		if (buffer_trash.size() > 0) {
			for (auto& pending : buffer_trash) {
				std::cout << "Destroying Buffer from Editor" << std::endl;
				vmaDestroyBuffer(engineDevice.allocator(), pending.buffer, pending.allocation);
			}
			buffer_trash.clear();
		}
		return;
	}
	
}