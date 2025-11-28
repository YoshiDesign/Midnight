
#include "Editor.h"
#include "Utils/Logger.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/swapchain.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/LinePipeline.h"
#include "CoreVK/PipelineLayout.h"
#include "Core/aveng_window.h"
#include "Core/Renderer/Renderer.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
#include <cassert>

namespace aveng {

	Editor::Editor(VkRenderData& _renderData, Renderer& _renderer, GameData& _gameData, EngineDevice& _engineDevice, AvengWindow& window, ModelAndInstanceData& modelInstanceData)
		: renderData{ _renderData }, renderer{_renderer}, gameData{ _gameData }, engineDevice{ _engineDevice }, window{ window }, mModelInstanceData{ modelInstanceData }
	{

		// Initial camera position
		editorViewerObject.transform.translation.z = -15.5f;
		editorViewerObject.transform.translation.y = -2.5f;

		renderData.rdLineDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdAvengSelectionDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdAvengAnimationSelectionDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		/* valid, but emtpy */
		mLineMesh = std::make_shared<VkLineMesh>();
		Logger::log(1, "%s: line mesh storage initialized\n", __FUNCTION__);

		mModelInstanceData.miInstanceCenterCallbackFunctionEditor = [this](std::shared_ptr<AssimpInstance> instance) { renderer.centerInstance(instance); };

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

	void Editor::updateCamera(float frameTime)
	{

		// Fetched all the way from downtown (the swapchain)
		aspect = getAspectRatio();

		// Track key press to transform viewer object
		keyboardController.moveCameraXZ(window.getGLFWwindow(), frameTime);

		// Apply new viewer obj values to the camera
		editor_camera.setViewYXZ(editorViewerObject.transform.translation + glm::vec3(0.f, 0.f, -.80f), editorViewerObject.transform.rotation);

		// Recalculate perspective
		editor_camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);

		// Update the data which the renderer reads from
		renderData.cameraProxy.projection = editor_camera.getProjection();
		renderData.cameraProxy.view = editor_camera.getView();
		
	}

	float Editor::getAspectRatio() {
		return renderer.getAspectRatio();
	}

	void Editor::startGame()
	{
		gameData.currentAppMode = AppMode::Game;
	}

	void Editor::render(unsigned int frameIndex, float frameTime)
	{
		currentFrameIndex = frameIndex;

		// This needs to occur within a valid renderpass
		aveng_imgui.newFrame();
		aveng_imgui.runGUI();
		aveng_imgui.render(frameIndex);

		updateCamera(frameTime);

		setupSelectionHighlight(frameTime);
		setSelectedInstance();
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
			"Can't end render pass on command buffer from a different frame");
		vkCmdEndRenderPass(commandBuffer);
	}

	void Editor::readPixelDataPos()
	{
		/* we must wait for the image to be created before we can pick  */
		if (editorData.eMousePick) {

			/* wait for queue to be idle */
			vkQueueWaitIdle(engineDevice.graphicsQueue());
			std::cout << "Mouse (From Editor): (" << editorData.eMouseXPos << ", " << editorData.eMouseYPos << ")" << std::endl;
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
			
			std::cout << "Positions Unsigned:" << static_cast<unsigned int>(editorData.eMouseXPos) << ", " << static_cast<unsigned int>(editorData.eMouseYPos) << std::endl;
			std::cout << "Positions Signed:" << static_cast<int>(editorData.eMouseXPos) << ", " << static_cast<int>(editorData.eMouseYPos) << std::endl;

			float selectedInstanceId = renderer.getPixelValueFromPos(editorData.eMouseXPos, editorData.eMouseYPos, currentFrameIndex);
			std::cout << "End Selection: " << selectedInstanceId << std::endl;
			if (selectedInstanceId >= 0.0f) {
				mModelInstanceData.miSelectedEditorInstance = static_cast<int>(selectedInstanceId);
			}
			else {
				mModelInstanceData.miSelectedEditorInstance = 0;
				editorData.eMousePick = false;
			}
			
			
		}
	}

	void Editor::init(SwapChain* swapchain) 
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

		swapchain->createEditorSelectionFramebuffers();

		createPipelineLayouts();

		std::string vertexShaderFile = "shaders/line.vert.spv";
		std::string fragmentShaderFile = "shaders/line.frag.spv";
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
	}

	bool Editor::createPipelineLayouts() {

		std::vector<VkPushConstantRange> pushConstants = { { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkPushConstants) } };

		/* selection, non-animated */
		std::vector<VkDescriptorSetLayout> selectionLayouts = {
		  renderData.rdAvengTextureDescriptorLayout,
		  renderData.rdAvengSelectionDescriptorLayout };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengSelectionPipelineLayout, selectionLayouts, pushConstants)) {
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
			if (!ShaderStorageBuffer::init(engineDevice, renderData.rdSelectedInstanceBuffers[i])) {
				Logger::log(1, "%s error: could not create selection SSBO\n", __FUNCTION__);
				return false;
			}
		}
	}

	/*
	* Set the currently selected instance (for highlight)
	*/
	void Editor::setupSelectionHighlight(float dt) {

		// Potential Improvement: Use a non - owning pointer or a reference to the shared_ptr in the container :
		// E.g.
		// AssimpInstance * currentSelectedInstance = nullptr;
		// if (mRenderData.rdHighlightSelectedInstance) {
		//     auto& sp = mModelInstData.miAssimpInstances[mModelInstData.miSelectedEditorInstance]; // reference, no refcount change
		//     currentSelectedInstance = sp.get(); // raw, non-owning
		// }
		/*
		* From ChatGuy:
		* If you prefer to keep safety when selection changes, cache a weak_ptr
		* once and lock only when you need it (ideally when selection changes, not per frame).
		*/

		editorData.eSelectedInstance.clear();
		// Q: Why is eSelectedInstance the size of all instances? A: As we iterate over instances, the selected one is set at that same index
		editorData.eSelectedInstance.resize(mModelInstanceData.miAssimpInstances.size());

		/* save the selected instance for color highlight */
		editorData.eCurrentSelectedInstance = nullptr;
		if (editorData.eHighlightSelectedInstance) {
			editorData.eCurrentSelectedInstance = mModelInstanceData.miAssimpInstances[mModelInstanceData.miSelectedEditorInstance];
			editorData.eSelectHighlightValue += dt * 4.0f;
			if (editorData.eSelectHighlightValue > 2.0f) {
				editorData.eSelectHighlightValue = 0.1f;
			}
		}
	}

	void Editor::handleMouseClick(const MouseButtonEvent& e) {
		aveng_imgui.handleMouseButtonEvents(e.button, e.action, e.mods);
	}

	void Editor::handleMouseMove(const MouseMoveEvent& e) {
		aveng_imgui.handleMousePositionEvents(e.x, e.y, e.rmbDown);
	}

	/*
	* Find the instance that was selected
	*/
	void Editor::setSelectedInstance()
	{

		size_t instanceToStore = 0;
		for (const auto& model : mModelInstanceData.miModelList) {
			size_t numberOfInstances = mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()].size();
			if (numberOfInstances > 0 && model->getTriangleCount() > 0) 
			{
				// std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()];
				//if (numberOfInstances > 0) 
				//{
				int index = 0;
				// for (unsigned int i = 0; i < numberOfInstances; ++i) {
				for (const auto& instance : mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()]) {

					// Set the buffer's x
					if (editorData.eCurrentSelectedInstance == instance) 
					{
						editorData.eSelectedInstance.at(instanceToStore + index).x = editorData.eSelectHighlightValue; // The blinking color
					} else {
						editorData.eSelectedInstance.at(instanceToStore + index).x = 1.0f; // color is unchanged
					}

					// Set the buffer's y
					if (editorData.eMousePick) 
					{
						InstanceSettings instSettings = instance->getInstanceSettings();
						editorData.eSelectedInstance.at(instanceToStore + index).y = static_cast<float>(instSettings.isInstanceIndexPosition);
					}
					index++;
				}
				//}
				instanceToStore += numberOfInstances;
			}
		}
	}

	bool Editor::drawInstanceGizmo() {

		if (!hasSelection()) return false;

		/* draw coordinate lines */
		mCoordArrowsLineIndexCount = 0;
		mLineMesh->vertices.clear();
		if (mModelInstanceData.miSelectedEditorInstance > 0) { // Make sure it's not the null instance. In other words, don't draw the gizmo's if the selected instance is the null instance (no selections)
			InstanceSettings instSettings = mModelInstanceData.miAssimpInstances.at(mModelInstanceData.miSelectedEditorInstance)->getInstanceSettings();

			/* draw coordiante arrows at origin of selected instance */
			switch (renderData.rdInstanceEditMode) {
			case instanceEditMode::move:
				mCoordArrowsMesh = mCoordArrowsModel.getVertexData();
				break;
			case instanceEditMode::rotate:
				mCoordArrowsMesh = mRotationArrowsModel.getVertexData();
				break;
			case instanceEditMode::scale:
				mCoordArrowsMesh = mScaleArrowsModel.getVertexData();
				break;
			}

			mCoordArrowsLineIndexCount += mCoordArrowsMesh.vertices.size();
			std::for_each(mCoordArrowsMesh.vertices.begin(), mCoordArrowsMesh.vertices.end(),
				[=](auto& n) {
				n.color /= 2.0f;
				n.position = glm::quat(glm::radians(instSettings.isWorldRotation)) * n.position;
				n.position += instSettings.isWorldPosition;
			});
			mLineMesh->vertices.insert(mLineMesh->vertices.end(),
				mCoordArrowsMesh.vertices.begin(), mCoordArrowsMesh.vertices.end());
		}

		if (!engineDevice.resetCommandBuffer(renderData.rdLineCommandBuffers[currentFrameIndex], 0)) {
			Logger::log(1, "%s error: failed to reset line drawing command buffer\n", __FUNCTION__);
			return false;
		}

		if (!engineDevice.beginSingleShotCommand(renderData.rdLineCommandBuffers[currentFrameIndex])) {
			Logger::log(1, "%s error: failed to begin line drawing command buffer\n", __FUNCTION__);
			return false;
		}

		// Begin - We can safely piggy back on the renderer's beginSwapChainRenderPass method - it's polymorphic
		renderer.beginSwapChainRenderPass(
			renderData.rdLineCommandBuffers[currentFrameIndex],
			renderer.getCurrentFramebuffer(),
			renderData.rdLineRenderpass);

		if (mCoordArrowsLineIndexCount > 0) {

			mUploadToVBOTimer.start();
			VertexBuffer::uploadData(engineDevice, mLineVertexBuffer, *mLineMesh);
			renderData.rdUploadToVBOTime += mUploadToVBOTimer.stop();

			vkCmdBindPipeline(renderData.rdLineCommandBuffers[currentFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdLinePipeline);

			vkCmdBindDescriptorSets(renderData.rdLineCommandBuffers[currentFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
				renderData.rdLinePipelineLayout, 0, 1, &renderData.rdLineDescriptorSets[currentFrameIndex], 0, nullptr);

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(renderData.rdLineCommandBuffers[currentFrameIndex], 0, 1, &mLineVertexBuffer.buffer, &offset);
			vkCmdSetLineWidth(renderData.rdLineCommandBuffers[currentFrameIndex], 3.0f);
			vkCmdDraw(renderData.rdLineCommandBuffers[currentFrameIndex], static_cast<uint32_t>(mLineMesh->vertices.size()), 1, 0, 0);

			// Fin - Specific end for Line renderpasses
			endSwapChainLineRenderPass(renderData.rdLineCommandBuffers[currentFrameIndex]);

			if (!engineDevice.endCommandBuffer(renderData.rdLineCommandBuffers[currentFrameIndex])) {
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
		std::cout << "getCurrentCommandBufferLines: " << currentFrameIndex << std::endl;
		assert(renderer.isFrameInProgress() && "Cannot get command buffer. The frame is not in progress.");
		return renderData.rdLineCommandBuffers[currentFrameIndex];
	}

	void Editor::endSwapChainLineRenderPass(VkCommandBuffer commandBuffer)
	{
		assert(renderer.isFrameInProgress() && "Can't call endSwapChain if frame is not in progress.");
		assert(commandBuffer == getCurrentCommandBufferLines() &&
			"Can't end render pass on command buffer from a different frame");
		vkCmdEndRenderPass(commandBuffer);
	}

	void Editor::drawSelectedModels(int frameIndex)
	{
		//
		renderer.drawModels(
			renderData.rdCommandBuffersGraphics[frameIndex],
			renderData.rdAvengSelectionPipeline,
			renderData.rdAvengAnimationSelectionPipeline,
			renderData.rdAvengSelectionPipelineLayout,
			renderData.rdAvengAnimationSelectionPipelineLayout,
			renderData.rdAvengSelectionDescriptorSets[frameIndex],
			renderData.rdAvengAnimationSelectionDescriptorSets[frameIndex],
			frameIndex);
	}

	void Editor::updateStorageBuffers()
	{
		// Upload the vec2 to make our selected instance highlight/blink
		bool bufferResized = false;
		bufferResized |= ShaderStorageBuffer::uploadSsboData(engineDevice, renderData.rdSelectedInstanceBuffers[currentFrameIndex], editorData.eSelectedInstance);
		
		// If one frame's buffer resized, resize ALL frames to keep them synchronized
		if (bufferResized) {
			size_t newBufferSize = renderData.rdSelectedInstanceBuffers[currentFrameIndex].bufferSize;
			for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
				if (i != currentFrameIndex) {
					ShaderStorageBuffer::checkForResize(engineDevice, renderData.rdSelectedInstanceBuffers[i], newBufferSize);
				}
			}
			
			std::cout << "StorageBuffer Resized - Updating Descriptor Sets" << std::endl;
			updateDescriptorSets();
			renderer.updateDescriptorSets();
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

			std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpSelUboBind, assimpSelSsboBind, assimpSelSsboBind2 };

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

			std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings =
			{ assimpSelUboBind, assimpSkinningSelSsboBind, assimpSkinningSelSsboBind2, assimpSkinningSelSsboBind3 };

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
		}

		updateDescriptorSets();

		return true;

	}

	void Editor::updateDescriptorSets()
	{
		Logger::log(1, "%s: updating descriptor sets\n", __FUNCTION__);
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++)
		{
			{
				/* selection shader, non-animated  */
				VkDescriptorBufferInfo matrixInfo{};
				matrixInfo.buffer = renderData.matrixBuffersView.viewProjUBOs[i].buffer;
				matrixInfo.offset = 0;
				matrixInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo worldPosInfo{};
				worldPosInfo.buffer = renderData.matrixBuffersView.modelRootSSBOs[i].buffer;
				worldPosInfo.offset = 0;
				worldPosInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo selectionInfo{};
				selectionInfo.buffer = renderData.rdSelectedInstanceBuffers[i].buffer;
				selectionInfo.offset = 0;
				selectionInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet matrixWriteDescriptorSet{};
				matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				matrixWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[i];
				matrixWriteDescriptorSet.dstBinding = 0;
				matrixWriteDescriptorSet.descriptorCount = 1;
				matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

				VkWriteDescriptorSet posWriteDescriptorSet{};
				posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				posWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[i];
				posWriteDescriptorSet.dstBinding = 1;
				posWriteDescriptorSet.descriptorCount = 1;
				posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

				VkWriteDescriptorSet selectionWriteDescriptorSet{};
				selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				selectionWriteDescriptorSet.dstSet = renderData.rdAvengSelectionDescriptorSets[i];
				selectionWriteDescriptorSet.dstBinding = 2;
				selectionWriteDescriptorSet.descriptorCount = 1;
				selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

				std::vector<VkWriteDescriptorSet> selectionWriteDescriptorSets =
				{ matrixWriteDescriptorSet, posWriteDescriptorSet, selectionWriteDescriptorSet };

				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(selectionWriteDescriptorSets.size()),
					selectionWriteDescriptorSets.data(), 0, nullptr);
			}

			{
				/* selection shader, animated  */
				VkDescriptorBufferInfo matrixInfo{};
				matrixInfo.buffer = renderData.matrixBuffersView.viewProjUBOs[i].buffer;
				matrixInfo.offset = 0;
				matrixInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo boneMatrixInfo{};
				boneMatrixInfo.buffer = renderData.matrixBuffersView.boneMatSSBOs[i].buffer;
				boneMatrixInfo.offset = 0;
				boneMatrixInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo worldPosInfo{};
				worldPosInfo.buffer = renderData.matrixBuffersView.modelRootSSBOs[i].buffer;
				worldPosInfo.offset = 0;
				worldPosInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo selectionInfo{};
				selectionInfo.buffer = renderData.rdSelectedInstanceBuffers[i].buffer;
				selectionInfo.offset = 0;
				selectionInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet matrixWriteDescriptorSet{};
				matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				matrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[i];
				matrixWriteDescriptorSet.dstBinding = 0;
				matrixWriteDescriptorSet.descriptorCount = 1;
				matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

				VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
				boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				boneMatrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[i];
				boneMatrixWriteDescriptorSet.dstBinding = 1;
				boneMatrixWriteDescriptorSet.descriptorCount = 1;
				boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

				VkWriteDescriptorSet posWriteDescriptorSet{};
				posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				posWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[i];
				posWriteDescriptorSet.dstBinding = 2;
				posWriteDescriptorSet.descriptorCount = 1;
				posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

				VkWriteDescriptorSet selectionWriteDescriptorSet{};
				selectionWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				selectionWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				selectionWriteDescriptorSet.dstSet = renderData.rdAvengAnimationSelectionDescriptorSets[i];
				selectionWriteDescriptorSet.dstBinding = 3;
				selectionWriteDescriptorSet.descriptorCount = 1;
				selectionWriteDescriptorSet.pBufferInfo = &selectionInfo;

				std::vector<VkWriteDescriptorSet> skinningSelectionWriteDescriptorSets =
				{ matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet,
				  posWriteDescriptorSet, selectionWriteDescriptorSet };

				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(skinningSelectionWriteDescriptorSets.size()),
					skinningSelectionWriteDescriptorSets.data(), 0, nullptr);
			}

			{
				/* line-drawing shader */
				VkDescriptorBufferInfo matrixInfo{};
				matrixInfo.buffer = renderData.matrixBuffersView.viewProjUBOs[i].buffer;
				matrixInfo.offset = 0;
				matrixInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet matrixWriteDescriptorSet{};
				matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				matrixWriteDescriptorSet.dstSet = renderData.rdLineDescriptorSets[i];
				matrixWriteDescriptorSet.dstBinding = 0;
				matrixWriteDescriptorSet.descriptorCount = 1;
				matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

				std::vector<VkWriteDescriptorSet> writeDescriptorSets =
				{ matrixWriteDescriptorSet };

				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(writeDescriptorSets.size()),
					writeDescriptorSets.data(), 0, nullptr);
			}
		}
	}

	void Editor::cleanup()
	{

		// Free command buffers
		vkFreeCommandBuffers(
			engineDevice.device(),
			engineDevice.commandPoolGraphics(),
			static_cast<uint32_t>(renderData.rdLineCommandBuffers.size()),
			renderData.rdLineCommandBuffers.data()
		);
		renderData.rdLineCommandBuffers.clear();

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			ShaderStorageBuffer::cleanup(engineDevice, renderData.rdSelectedInstanceBuffers[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdAvengSelectionDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdAvengAnimationSelectionDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.editorDescriptorPool, 1, &renderData.rdLineDescriptorSets[i]);
		}

		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengSelectionDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengAnimationSelectionDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdLineDescriptorLayout, nullptr);

		vkDestroyRenderPass(engineDevice.device(), renderData.rdLineRenderpass, nullptr);
		vkDestroyRenderPass(engineDevice.device(), renderData.rdSelectionRenderpass, nullptr);
	}
	
}