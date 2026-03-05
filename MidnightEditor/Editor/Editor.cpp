
#include "Editor.h"
#include "Editor/Utils/selection_utils.h"
#include "Utils/Logger.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/swapchain.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/LinePipeline.h"
#include "CoreVK/PipelineLayout.h"
#include "Core/aveng_window.h"
#include "Core/Renderer/Renderer.h"
#include "Core/Input/InputState.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "Runtime/World/InstanceManager.h"
#include "Runtime/Facade/SceneFacade.h"

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

		renderData.rdLineDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdAvengSelectionDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdAvengAnimationSelectionDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		/* valid, but emtpy */
		mLineMesh = std::make_shared<VkLineMesh>();
		// Logger::log(1, "%s: line mesh storage initialized\n", __FUNCTION__);

		if (!createCommandBuffers())
		{
			throw std::runtime_error("editor failed to create command buffers");
		}

		if (!createSSBOs())
		{
			throw std::runtime_error("editor failed to create storage buffers");
		}

		if (!createDescriptorLayouts())
		{
			throw std::runtime_error("editor failed to create descriptor layouts");
		}
		if (!createDescriptorSets())
		{
			throw std::runtime_error("editor failed to create descriptor sets");
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

		//setupSelectionHighlight(frameTime);
		//setSelectedInstance();
		updateStorageBuffers();
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

	void Editor::initializePointLights()
	{

		pointLightSystem.initialize(renderData.rdSelectionRenderpass, 1, false);
		std::cout << "(Editor) PointLightSystem initialized" << std::endl;

	}

	void Editor::readPixelDataPos()
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

			editorData.primarySelection = renderer.getPickedHandle(pickId);
			editorData.selectedMany.clear();
			editorData.eShowTRSPanel = true; // Not necessary here, just being explicit
			addUnique(editorData.selectedMany, editorData.primarySelection);

			//std::cout << "Picked Handle Index: " << editorData.primarySelection.index;
		}
		else {
			std::cout << "False Selection: SelectedID\t" << pickId << std::endl;
			std::cout << "Deselecting instance:\t" << renderData.selectedPickId << std::endl;
			renderData.selectedPickId = 0;

			editorData.primarySelection = AnyInstanceHandle{};
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
		createPipelineLayouts();

		// Debug - Static
		std::string vertexShaderFile = "shaders/debug.vert.spv";
		std::string fragmentShaderFile = "shaders/debug.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdDebugPipelineLayout,
			renderData.rdDebugPipeline, renderData.rdSelectionRenderpass, 2, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 0");
		}

		// Debug - Animated
		vertexShaderFile = "shaders/debug_skinning.vert.spv";
		fragmentShaderFile = "shaders/debug_skinning.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdDebugAnimatedPipelineLayout,
			renderData.rdDebugAnimatedPipeline, renderData.rdSelectionRenderpass, 2, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 0");
		}

		vertexShaderFile = "shaders/line.vert.spv";
		fragmentShaderFile = "shaders/line.frag.spv";
		if (!LinePipeline::init(engineDevice, renderData.rdLineRenderpass, renderData.rdLinePipelineLayout,
			renderData.rdLinePipeline, vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init Assimp line drawing shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 1");
		}

		vertexShaderFile = "shaders/aveng_selection.vert.spv";
		fragmentShaderFile = "shaders/aveng_selection.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdAvengSelectionPipelineLayout,
			renderData.rdAvengSelectionPipeline, renderData.rdSelectionRenderpass, 2,
			vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init aveng Selection shader pipeline\n", __FUNCTION__);
			throw std::runtime_error("editor fail 2");
		}

		vertexShaderFile = "shaders/aveng_skinning_selection.vert.spv";
		fragmentShaderFile = "shaders/aveng_skinning_selection.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdAvengAnimationSelectionPipelineLayout,
			renderData.rdAvengAnimationSelectionPipeline, renderData.rdSelectionRenderpass, 2,
			vertexShaderFile, fragmentShaderFile)) {
			Logger::log(1, "%s error: could not init aveng Skinning Selection shader pipeline\n", __FUNCTION__);
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

	void Editor::updateLights()
	{
		renderer.renderLights(pointLightSystem.getPipeline(), pointLightSystem.getPipelineLayout());
	}
	
	bool Editor::createPipelineLayouts() {

		std::vector<VkPushConstantRange> pushConstants = { { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkPushConstants) } };

		/* selection, non-animated */
		std::vector<VkDescriptorSetLayout> selectionLayouts = {
		  renderData.rdAvengTextureDescriptorLayout,
		  renderData.rdAvengSelectionDescriptorLayout };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengSelectionPipelineLayout, selectionLayouts, pushConstants)) {
			Logger::log(1, "%s error: could not init Assimp selection pipeline layout\n", __FUNCTION__);
			return false;
		}
		/* Static debug */
		if (!PipelineLayout::init(engineDevice, renderData.rdDebugPipelineLayout, selectionLayouts, pushConstants)) {
			Logger::log(1, "%s error: could not init Assimp selection pipeline layout\n", __FUNCTION__);
			return false;
		}

		/* selection, animated */
		std::vector<VkDescriptorSetLayout> skinningSelectionLayouts = {
		  renderData.rdAvengTextureDescriptorLayout,
		  renderData.rdAvengAnimationSelectionDescriptorLayout };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengAnimationSelectionPipelineLayout, skinningSelectionLayouts, pushConstants)) {
			Logger::log(1, "%s error: could not init Assimp skinning selection pipeline layout\n", __FUNCTION__);
			return false;
		}
		
		/* Animated debug */
		if (!PipelineLayout::init(engineDevice, renderData.rdDebugAnimatedPipelineLayout, skinningSelectionLayouts, pushConstants)) {
			Logger::log(1, "%s error: could not init Assimp skinning selection pipeline layout\n", __FUNCTION__);
			return false;
		}

		/* line drawing */
		std::vector<VkDescriptorSetLayout> lineLayouts = {
		  renderData.rdLineDescriptorLayout };

		if (!PipelineLayout::init(engineDevice, renderData.rdLinePipelineLayout, lineLayouts)) {
			Logger::log(1, "%s error: could not init Assimp line drawing pipeline layout\n", __FUNCTION__);
			return false;
		}
	
		return true;
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

	bool Editor::createSSBOs()
	{
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			if (!ShaderStorageBuffer::init(engineDevice, renderData.rdSelectedInstanceBuffers[i], MapMode::Persistent, ResidentMode::CPU)) {
				Logger::log(1, "%s error: could not create selection SSBO\n", __FUNCTION__);
				return false;
			}
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

			vkCmdBindDescriptorSets(renderData.rdLineCommandBuffers.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_GRAPHICS,
				renderData.rdLinePipelineLayout, 0, 1, &renderData.rdLineDescriptorSets.at(currentFrameIndex), 0, nullptr);

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

	void Editor::drawModels(const IModelLibrary& modelLib, int frameIndex)
	{
		//
		//renderer.drawModels(
		//	modelLib,
		//	renderData.rdCommandBuffersGraphics[frameIndex],
		//	renderData.rdAvengSelectionPipeline,
		//	renderData.rdAvengAnimationSelectionPipeline,
		//	renderData.rdAvengSelectionPipelineLayout,
		//	renderData.rdAvengAnimationSelectionPipelineLayout,
		//	renderData.rdAvengSelectionDescriptorSets[frameIndex],
		//	renderData.rdAvengAnimationSelectionDescriptorSets[frameIndex],
		//	frameIndex);
		//
		renderer.drawModels(
			modelLib,
			renderData.rdCommandBuffersGraphics[frameIndex],
			renderData.rdDebugPipeline,
			renderData.rdDebugAnimatedPipeline,
			renderData.rdDebugPipelineLayout,
			renderData.rdDebugAnimatedPipelineLayout,
			renderData.rdAvengSelectionDescriptorSets[frameIndex],
			renderData.rdAvengAnimationSelectionDescriptorSets[frameIndex],
			frameIndex);
	}

	void Editor::updateStorageBuffers()
	{

		renderer.updateBufferViews(); // TODO: Remove this once stable

		// If one frame's buffer resized, resize ALL frames to keep them synchronized
		if (ShaderStorageBuffer::uploadSsboData(engineDevice, renderData.rdSelectedInstanceBuffers.at(currentFrameIndex), editorData.eSelectedInstance)) {

			buffer_trash.push_back(PendingBufferDestroy {
					renderData.rdSelectedInstanceBuffers.at(currentFrameIndex).buffer,
					renderData.rdSelectedInstanceBuffers.at(currentFrameIndex).bufferAlloc
				}
			);

			size_t newBufferSize = std::max(editorData.eSelectedInstance.size() * (sizeof glm::vec2), renderData.rdSelectedInstanceBuffers.at(currentFrameIndex).bufferSize * 2);
			
			ShaderStorageBuffer::init(engineDevice, renderData.rdSelectedInstanceBuffers.at(currentFrameIndex), MapMode::Persistent, ResidentMode::CPU, newBufferSize);

			if (ShaderStorageBuffer::uploadSsboData(engineDevice, renderData.rdSelectedInstanceBuffers.at(currentFrameIndex), editorData.eSelectedInstance))
			{
				std::cout << "[3] Unable to resize SSBO" << std::endl;
				throw std::runtime_error("[3] Unable to resize SSBO");
			}
			
			std::cout << "[Editor] StorageBuffer Resized - Updating Descriptor Sets" << std::endl;
			updateDescriptorSets(currentFrameIndex);
			renderer.updateDescriptorSets(currentFrameIndex);
		}

	}

	bool Editor::createDescriptorLayouts()
	{
		{
			/* non-animated selection shader */
			VkDescriptorSetLayoutBinding assimpSelUboBind{};
			assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpSelUboBind.binding = 0;
			assimpSelUboBind.descriptorCount = 1;
			assimpSelUboBind.pImmutableSamplers = nullptr;
			assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSelSsboBind{};
			assimpSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSelSsboBind.binding = 1;
			assimpSelSsboBind.descriptorCount = 1;
			assimpSelSsboBind.pImmutableSamplers = nullptr;
			assimpSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSelSsboBind2{};
			assimpSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSelSsboBind2.binding = 2;
			assimpSelSsboBind2.descriptorCount = 1;
			assimpSelSsboBind2.pImmutableSamplers = nullptr;
			assimpSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			// Lighting Uniform - Point Lights
			VkDescriptorSetLayoutBinding assimpSkinningSsboBind3{};
			assimpSkinningSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpSkinningSsboBind3.binding = 3;
			assimpSkinningSsboBind3.descriptorCount = 1;
			assimpSkinningSsboBind3.pImmutableSamplers = nullptr;
			assimpSkinningSsboBind3.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpSelUboBind, assimpSelSsboBind, assimpSelSsboBind2, assimpSkinningSsboBind3 };

			VkDescriptorSetLayoutCreateInfo assimpSelCreateInfo{};
			assimpSelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpSelCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
			assimpSelCreateInfo.pBindings = assimpBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpSelCreateInfo,
				nullptr, &renderData.rdAvengSelectionDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}

		{
			/* animated selection shader */
			VkDescriptorSetLayoutBinding assimpSelUboBind{};
			assimpSelUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpSelUboBind.binding = 0;
			assimpSelUboBind.descriptorCount = 1;
			assimpSelUboBind.pImmutableSamplers = nullptr;
			assimpSelUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind{};
			assimpSkinningSelSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSkinningSelSsboBind.binding = 1;
			assimpSkinningSelSsboBind.descriptorCount = 1;
			assimpSkinningSelSsboBind.pImmutableSamplers = nullptr;
			assimpSkinningSelSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind2{};
			assimpSkinningSelSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSkinningSelSsboBind2.binding = 2;
			assimpSkinningSelSsboBind2.descriptorCount = 1;
			assimpSkinningSelSsboBind2.pImmutableSamplers = nullptr;
			assimpSkinningSelSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSkinningSelSsboBind3{};
			assimpSkinningSelSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSkinningSelSsboBind3.binding = 3;
			assimpSkinningSelSsboBind3.descriptorCount = 1;
			assimpSkinningSelSsboBind3.pImmutableSamplers = nullptr;
			assimpSkinningSelSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			// Lighting Uniform - Point Lights
			VkDescriptorSetLayoutBinding assimpSkinningSsboBind4{};
			assimpSkinningSsboBind4.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpSkinningSsboBind4.binding = 4;
			assimpSkinningSsboBind4.descriptorCount = 1;
			assimpSkinningSsboBind4.pImmutableSamplers = nullptr;
			assimpSkinningSsboBind4.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
			{ assimpSelUboBind, assimpSkinningSelSsboBind, assimpSkinningSelSsboBind2, assimpSkinningSelSsboBind3, assimpSkinningSsboBind4 };

			VkDescriptorSetLayoutCreateInfo assimpSkinningCreateInfo{};
			assimpSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
			assimpSkinningCreateInfo.pBindings = assimpSkinningBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpSkinningCreateInfo,
				nullptr, &renderData.rdAvengAnimationSelectionDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp skinning selection buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}
	
		{
			/* line shader */
			VkDescriptorSetLayoutBinding assimpUboBind{};
			assimpUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpUboBind.binding = 0;
			assimpUboBind.descriptorCount = 1;
			assimpUboBind.pImmutableSamplers = nullptr;
			assimpUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpUboBind };

			VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
			assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
			assimpCreateInfo.pBindings = assimpBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpCreateInfo,
				nullptr, &renderData.rdLineDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp line drawing descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}
	
		return true;
	}

	bool Editor::createDescriptorSets()
	{

		std::vector<VkDescriptorPoolSize> poolSizes =
		{
		  { VK_DESCRIPTOR_TYPE_SAMPLER, 500 },
		  { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 500 },
		  { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 500 },
		  { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 500 },
		  { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 500 },
		  { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 500 },
		};

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 500;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		VkResult result = vkCreateDescriptorPool(engineDevice.device(), &poolInfo, nullptr, &renderData.editorDescriptorPool);
		if (result != VK_SUCCESS) {
			Logger::log(1, "%s error: could not init descriptor pool (error: %i)\n", __FUNCTION__, result);
			return false;
		}

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			{
				/* line-drawing */
				VkDescriptorSetAllocateInfo lineAllocateInfo{};
				lineAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				lineAllocateInfo.descriptorPool = renderData.editorDescriptorPool;
				lineAllocateInfo.descriptorSetCount = 1;
				lineAllocateInfo.pSetLayouts = &renderData.rdLineDescriptorLayout;

				VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &lineAllocateInfo,
					&renderData.rdLineDescriptorSets[i]);
				if (result != VK_SUCCESS) {
					Logger::log(1, "%s error: could not allocate Assimp line-drawing descriptor set (error: %i)\n", __FUNCTION__, result);
					return false;
				}
			}
			{
				/* selection, non-animated models */
				VkDescriptorSetAllocateInfo selectionDescriptorAllocateInfo{};
				selectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				selectionDescriptorAllocateInfo.descriptorPool = renderData.editorDescriptorPool;
				selectionDescriptorAllocateInfo.descriptorSetCount = 1;
				selectionDescriptorAllocateInfo.pSetLayouts = &renderData.rdAvengSelectionDescriptorLayout;

				VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &selectionDescriptorAllocateInfo,
					&renderData.rdAvengSelectionDescriptorSets[i]);
				if (result != VK_SUCCESS) {
					Logger::log(1, "%s error: could not allocate Assimp selection descriptor set (error: %i)\n", __FUNCTION__, result);
					return false;
				}
			}
			{
				/* selection, animated models */
				VkDescriptorSetAllocateInfo skinningSelectionDescriptorAllocateInfo{};
				skinningSelectionDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
				skinningSelectionDescriptorAllocateInfo.descriptorPool = renderData.editorDescriptorPool;
				skinningSelectionDescriptorAllocateInfo.descriptorSetCount = 1;
				skinningSelectionDescriptorAllocateInfo.pSetLayouts = &renderData.rdAvengAnimationSelectionDescriptorLayout;

				VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &skinningSelectionDescriptorAllocateInfo,
					&renderData.rdAvengAnimationSelectionDescriptorSets[i]);
				if (result != VK_SUCCESS) {
					Logger::log(1, "%s error: could not allocate Assimp skinning selection descriptor set (error: %i)\n", __FUNCTION__, result);
					return false;
				}
			}

			updateDescriptorSets(i);
		}

		return true;

	}

	void Editor::updateDescriptorSets(int frameIndex)
	{

		{
			/* selection shader, non-animated  */
			VkDescriptorBufferInfo matrixInfo{};
			matrixInfo.buffer = renderData.matrixBuffersView.viewProjUBOs[frameIndex].buffer;
			matrixInfo.offset = 0;
			matrixInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo worldPosInfo{};
			worldPosInfo.buffer = renderData.matrixBuffersView.modelRootSSBOs[frameIndex].buffer;
			worldPosInfo.offset = 0;
			worldPosInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo selectionInfo{};
			selectionInfo.buffer = renderData.rdSelectedInstanceBuffers[frameIndex].buffer;
			selectionInfo.offset = 0;
			selectionInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo lightsInfo{};
			lightsInfo.buffer = renderData.pointLightBufferView.viewPointLightUBOs[frameIndex].buffer;
			lightsInfo.offset = 0;
			lightsInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet matrixWriteDescriptorSet{};
			matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			matrixWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[frameIndex];
			matrixWriteDescriptorSet.dstBinding = 0;
			matrixWriteDescriptorSet.descriptorCount = 1;
			matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

			VkWriteDescriptorSet posWriteDescriptorSet{};
			posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			posWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[frameIndex];
			posWriteDescriptorSet.dstBinding = 1;
			posWriteDescriptorSet.descriptorCount = 1;
			posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

			VkWriteDescriptorSet selectionWriteDescriptorSet{};
			selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			selectionWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[frameIndex];
			selectionWriteDescriptorSet.dstBinding = 2;
			selectionWriteDescriptorSet.descriptorCount = 1;
			selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

			VkWriteDescriptorSet lightsWriteDescriptorSet{};
			lightsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			lightsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			lightsWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[frameIndex];
			lightsWriteDescriptorSet.dstBinding = 3;
			lightsWriteDescriptorSet.descriptorCount = 1;
			lightsWriteDescriptorSet.pBufferInfo = &lightsInfo;

			std::vector<VkWriteDescriptorSet> selectionWriteDescriptorSets =
			{ matrixWriteDescriptorSet, posWriteDescriptorSet, selectionWriteDescriptorSet, lightsWriteDescriptorSet };

			vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(selectionWriteDescriptorSets.size()),
				selectionWriteDescriptorSets.data(), 0, nullptr);
		}

		{
			/* selection shader, animated  */
			VkDescriptorBufferInfo matrixInfo{};
			matrixInfo.buffer = renderData.matrixBuffersView.viewProjUBOs[frameIndex].buffer;
			matrixInfo.offset = 0;
			matrixInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo boneMatrixInfo{};
			boneMatrixInfo.buffer = renderData.matrixBuffersView.boneMatSSBOs[frameIndex].buffer;
			boneMatrixInfo.offset = 0;
			boneMatrixInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo worldPosInfo{};
			worldPosInfo.buffer = renderData.matrixBuffersView.modelRootSSBOs[frameIndex].buffer;
			worldPosInfo.offset = 0;
			worldPosInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo selectionInfo{};
			selectionInfo.buffer = renderData.rdSelectedInstanceBuffers[frameIndex].buffer;
			selectionInfo.offset = 0;
			selectionInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo lightsInfo{};
			lightsInfo.buffer = renderData.pointLightBufferView.viewPointLightUBOs[frameIndex].buffer;
			lightsInfo.offset = 0;
			lightsInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet matrixWriteDescriptorSet{};
			matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			matrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[frameIndex];
			matrixWriteDescriptorSet.dstBinding = 0;
			matrixWriteDescriptorSet.descriptorCount = 1;
			matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

			VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
			boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			boneMatrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[frameIndex];
			boneMatrixWriteDescriptorSet.dstBinding = 1;
			boneMatrixWriteDescriptorSet.descriptorCount = 1;
			boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

			VkWriteDescriptorSet posWriteDescriptorSet{};
			posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			posWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[frameIndex];
			posWriteDescriptorSet.dstBinding = 2;
			posWriteDescriptorSet.descriptorCount = 1;
			posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

			VkWriteDescriptorSet selectionWriteDescriptorSet{};
			selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			selectionWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[frameIndex];
			selectionWriteDescriptorSet.dstBinding = 3;
			selectionWriteDescriptorSet.descriptorCount = 1;
			selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;
			
			VkWriteDescriptorSet lightsWriteDescriptorSet{};
			lightsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			lightsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			lightsWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[frameIndex];
			lightsWriteDescriptorSet.dstBinding = 4;
			lightsWriteDescriptorSet.descriptorCount = 1;
			lightsWriteDescriptorSet.pBufferInfo = &lightsInfo;

			std::vector<VkWriteDescriptorSet> skinningSelectionWriteDescriptorSets =
			{ matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet,
				posWriteDescriptorSet, selectionWriteDescriptorSet, lightsWriteDescriptorSet };

			vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(skinningSelectionWriteDescriptorSets.size()),
				skinningSelectionWriteDescriptorSets.data(), 0, nullptr);
		}

		{
			/* line-drawing shader */
			VkDescriptorBufferInfo matrixInfo{};
			matrixInfo.buffer = renderData.matrixBuffersView.viewProjUBOs[frameIndex].buffer;
			matrixInfo.offset = 0;
			matrixInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet matrixWriteDescriptorSet{};
			matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			matrixWriteDescriptorSet.dstSet = renderData.rdLineDescriptorSets[frameIndex];
			matrixWriteDescriptorSet.dstBinding = 0;
			matrixWriteDescriptorSet.descriptorCount = 1;
			matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

			std::vector<VkWriteDescriptorSet> writeDescriptorSets =
			{ matrixWriteDescriptorSet };

			vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(writeDescriptorSets.size()),
				writeDescriptorSets.data(), 0, nullptr);
		}
	
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

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			ShaderStorageBuffer::cleanup(engineDevice, renderData.rdSelectedInstanceBuffers[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdAvengSelectionDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdAvengAnimationSelectionDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdLineDescriptorSets[i]);
		}
		
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengSelectionDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengAnimationSelectionDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdLineDescriptorLayout, nullptr);

		vkDestroyPipeline(engineDevice.device(), renderData.rdDebugPipeline, nullptr);
		vkDestroyPipeline(engineDevice.device(), renderData.rdDebugAnimatedPipeline, nullptr);
		vkDestroyPipeline(engineDevice.device(), renderData.rdAvengSelectionPipeline, nullptr);
		vkDestroyPipeline(engineDevice.device(), renderData.rdAvengAnimationSelectionPipeline, nullptr);
		vkDestroyPipeline(engineDevice.device(), renderData.rdLinePipeline, nullptr);

		vkDestroyPipelineLayout(engineDevice.device(), renderData.rdDebugPipelineLayout, nullptr);
		vkDestroyPipelineLayout(engineDevice.device(), renderData.rdDebugAnimatedPipelineLayout, nullptr);
		vkDestroyPipelineLayout(engineDevice.device(), renderData.rdAvengSelectionPipelineLayout, nullptr);
		vkDestroyPipelineLayout(engineDevice.device(), renderData.rdAvengAnimationSelectionPipelineLayout, nullptr);
		vkDestroyPipelineLayout(engineDevice.device(), renderData.rdLinePipelineLayout, nullptr);

		vkDestroyRenderPass(engineDevice.device(), renderData.rdLineRenderpass, nullptr);
		vkDestroyRenderPass(engineDevice.device(), renderData.rdSelectionRenderpass, nullptr);
		vkDestroyRenderPass(engineDevice.device(), renderData.rdImguiRenderpass, nullptr);

		vkDestroyDescriptorPool(engineDevice.device(), renderData.editorDescriptorPool, nullptr);
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