#include "Renderer.h"
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <cstdio>
#include <array>

#define LOG(a) std::cout<<a<<std::endl;
#define DESTROY_UNIFORM_BUFFERS 1	// Unused as far as I can tell

namespace aveng {

	Renderer::Renderer(AvengWindow& window, GameData& _gameData) : aveng_window{ window }, gameData{_gameData}
	{

		recreateSwapChain();

		createCommandBuffers();

		//sceneLoader.load(default_scene_file, engineDevice);

		// Initialize our descriptor sets, map buffers to device memory.
		setupDescriptors();

		// Create pipelines now that descriptor layouts are ready
		if (!createPipelineLayouts()) {
			std::cerr << "error [Renderer 1]!" << std::endl;
		}

		if (!createPipelines()) {
			std::cerr << "error [Renderer 2]!" << std::endl;
		}

		if (!createSyncObjects()) {
			std::cerr << "error [Renderer 3]!" << std::endl;
		}

		// Initialize PointLightSystem now that descriptor layouts are created
		// initializePointLightSystem();

		editor.init(window, aveng_swapchain.get());

		/* register callbacks */
		mModelInstanceData.miModelCheckCallbackFunction = [this](std::string fileName) { return hasModel(fileName); };
		mModelInstanceData.miModelAddCallbackFunction = [this](std::string fileName) { return addModel(fileName); };
		mModelInstanceData.miModelDeleteCallbackFunction = [this](std::string modelName) { deleteModel(modelName); };

		mModelInstanceData.miInstanceAddCallbackFunction = [this](std::shared_ptr<AvengModel> model) { return addInstance(model); };
		mModelInstanceData.miInstanceAddManyCallbackFunction = [this](std::shared_ptr<AvengModel> model, int numInstances) { addInstances(model, numInstances); };
		mModelInstanceData.miInstanceDeleteCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance) { deleteInstance(instance); };
		mModelInstanceData.miInstanceCloneCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance) { cloneInstance(instance); };

		/* signal graphics semaphores before doing anything else to be able to run compute submit */
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = &renderData.rdGraphicSemaphore[i];

			VkResult result = vkQueueSubmit(engineDevice.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to submit initial semaphore for frame %i (%i)\n", __FUNCTION__, i, result);
			}
		}
		vkQueueWaitIdle(engineDevice.graphicsQueue());

		mFrameTimer.start();
		std::printf("%s: Vulkan renderer initialized!\n");

	}

	Renderer::~Renderer()
	{
		std::cout << "Destroying Renderer..." << std::endl;
		freeCommandBuffers();
		cleanup();
		vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
	}


	bool Renderer::hasModel(std::string modelFileName) {
		auto modelIter = std::find_if(mModelInstanceData.miModelList.begin(), mModelInstanceData.miModelList.end(),
			[modelFileName](const auto& model) {
			return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
		});
		return modelIter != mModelInstanceData.miModelList.end();
	}

	std::shared_ptr<AvengModel> Renderer::getModel(std::string modelFileName) {
		auto modelIter = std::find_if(mModelInstanceData.miModelList.begin(), mModelInstanceData.miModelList.end(),
			[modelFileName](const auto& model) {
			return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
		});
		if (modelIter != mModelInstanceData.miModelList.end()) {
			return *modelIter;
		}
		return nullptr;
	}

	bool Renderer::addModel(std::string modelFileName) {
		if (hasModel(modelFileName)) {
			std::printf("%s warning: model '%s' already existed, skipping\n", __FUNCTION__, modelFileName.c_str());
			return false;
		}

		std::shared_ptr<AvengModel> model = std::make_shared<AvengModel>(engineDevice, renderData, modelFileName);
		if (!model->loadModelV2(renderData, modelFileName)) {
			std::printf("%s error: could not load model file '%s'\n", __FUNCTION__, modelFileName.c_str());
			return false;
		}

		mModelInstanceData.miModelList.emplace_back(model);

		/* also add a new instance here to see the model */
		addInstance(model);

		return true;
	}

	void Renderer::deleteModel(std::string modelFileName) {
		std::string shortModelFileName = std::filesystem::path(modelFileName).filename().generic_string();

		if (!mModelInstanceData.miAssimpInstances.empty()) {
			mModelInstanceData.miAssimpInstances.erase(
				std::remove_if(
					mModelInstanceData.miAssimpInstances.begin(),
					mModelInstanceData.miAssimpInstances.end(),
					[shortModelFileName](std::shared_ptr<AssimpInstance> instance) {
				return instance->getModel()->getModelFileName() == shortModelFileName;
			}
				),
				mModelInstanceData.miAssimpInstances.end()
			);
		}

		if (mModelInstanceData.miAssimpInstancesPerModel.count(shortModelFileName) > 0) {
			mModelInstanceData.miAssimpInstancesPerModel[shortModelFileName].clear();
			mModelInstanceData.miAssimpInstancesPerModel.erase(shortModelFileName);
		}

		/* add models to pending delete list */
		for (const auto& model : mModelInstanceData.miModelList) {
			if (model && (model->getTriangleCount() > 0)) {
				mModelInstanceData.miPendingDeleteAvengModels.insert(model);
			}
		}

		mModelInstanceData.miModelList.erase(
			std::remove_if(
				mModelInstanceData.miModelList.begin(),
				mModelInstanceData.miModelList.end(),
				[modelFileName](std::shared_ptr<AvengModel> model) {
			return model->getModelFileName() == modelFileName;
		}
			)
		);

		updateTriangleCount();
	}

	std::shared_ptr<AssimpInstance> Renderer::addInstance(std::shared_ptr<AvengModel> model) {
		std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model);
		mModelInstanceData.miAssimpInstances.emplace_back(newInstance);
		mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);

		updateTriangleCount();

		return newInstance;
	}

	void Renderer::addInstances(std::shared_ptr<AvengModel> model, int numInstances) {
		size_t animClipNum = model->getAnimClips().size();
		for (int i = 0; i < numInstances; ++i) {
			int xPos = std::rand() % 50 - 25;
			int zPos = std::rand() % 50 - 25;
			int rotation = std::rand() % 360 - 180;
			int clipNr = std::rand() % animClipNum;

			std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model, glm::vec3(xPos, 0.0f, zPos), glm::vec3(0.0f, rotation, 0.0f));
			if (animClipNum > 0) {
				InstanceSettings instSettings = newInstance->getInstanceSettings();
				instSettings.isAnimClipNr = clipNr;
				newInstance->setInstanceSettings(instSettings);
			}

			mModelInstanceData.miAssimpInstances.emplace_back(newInstance);
			mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
		}
		updateTriangleCount();
	}

	void Renderer::deleteInstance(std::shared_ptr<AssimpInstance> instance) {
		std::shared_ptr<AvengModel> currentModel = instance->getModel();
		std::string currentModelName = currentModel->getModelFileName();

		mModelInstanceData.miAssimpInstances.erase(
			std::remove_if(
				mModelInstanceData.miAssimpInstances.begin(),
				mModelInstanceData.miAssimpInstances.end(),
				[instance](std::shared_ptr<AssimpInstance> inst) {
			return inst == instance;
		}
			));


		mModelInstanceData.miAssimpInstancesPerModel[currentModelName].erase(
			std::remove_if(
				mModelInstanceData.miAssimpInstancesPerModel[currentModelName].begin(),
				mModelInstanceData.miAssimpInstancesPerModel[currentModelName].end(),
				[instance](std::shared_ptr<AssimpInstance> inst) {
			return inst == instance;
		}
			));

		updateTriangleCount();
	}

	void Renderer::cloneInstance(std::shared_ptr<AssimpInstance> instance) {
		std::shared_ptr<AvengModel> currentModel = instance->getModel();
		std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(currentModel);
		InstanceSettings newInstanceSettings = instance->getInstanceSettings();

		/* slight offset to see new instance */
		newInstanceSettings.isWorldPosition += glm::vec3(1.0f, 0.0f, -1.0f);
		newInstance->setInstanceSettings(newInstanceSettings);

		mModelInstanceData.miAssimpInstances.emplace_back(newInstance);
		mModelInstanceData.miAssimpInstancesPerModel[currentModel->getModelFileName()].emplace_back(newInstance);

		updateTriangleCount();
	}

	void Renderer::updateTriangleCount() {
		renderData.rdTriangleCount = 0;
		for (const auto& instance : mModelInstanceData.miAssimpInstances) {
			renderData.rdTriangleCount += instance->getModel()->getTriangleCount();
		}
	}

	//void Renderer::loadScenes(const char* filepath)
	//{

	//}

	/*
	* Important: Recreating the swapchain isn't sufficient if the image format changes
	* such as the window being moved to a different monitor. In that event it would
	* be necessary to recreate the renderpass.
	 */
	void Renderer::recreateSwapChain()
	{

		/**
		* TODO - Re-initialize the Editor's swapchain too
		*/

		// Get current window size
		auto extent = aveng_window.getExtent();

		// If the program has at least 1 dimension of 0 size (it's minimized); wait
		while (extent.width == 0 || extent.height == 0)
		{
			extent = aveng_window.getExtent();
			glfwWaitEvents();
		}

		// Wait until the current swap chain isn't being used before we attempt to construct the next one.
		vkDeviceWaitIdle(engineDevice.device());
	
		aveng_swapchain = nullptr;

		if (aveng_swapchain == nullptr) {
			// Create the new swapchain object
			aveng_swapchain = std::make_unique<SwapChain>(engineDevice, extent);
		}
		else {
			// 
			std::shared_ptr<SwapChain> oldSwapChain = std::move(aveng_swapchain);
			aveng_swapchain = std::make_unique<SwapChain>(engineDevice, extent, oldSwapChain);

			if (!oldSwapChain->compareSwapFormats(*aveng_swapchain.get()))
			{
				throw std::runtime_error("Swap chain image format or depth format has changed.");
			}

		}

	}

	void Renderer::createCommandBuffers() {

		// Resize our vector of command buffers to match the max number of images the swapchain will allow in 
		renderData.rdCommandBuffersGraphics.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdCommandBuffersCompute.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		engineDevice.initCommandBuffers(renderData.rdCommandBuffersGraphics);
		engineDevice.initCommandBuffers(renderData.rdCommandBuffersCompute, "compute");

	}

	void Renderer::freeCommandBuffers()
	{
		vkFreeCommandBuffers(
			engineDevice.device(),
			engineDevice.commandPoolGraphics(),
			static_cast<uint32_t>(renderData.rdCommandBuffersGraphics.size()),
			renderData.rdCommandBuffersGraphics.data()
		);

		vkFreeCommandBuffers(
			engineDevice.device(),
			engineDevice.commandPoolCompute(),
			static_cast<uint32_t>(renderData.rdCommandBuffersCompute.size()),
			renderData.rdCommandBuffersCompute.data()
		);

		renderData.rdCommandBuffersGraphics.clear();
		renderData.rdCommandBuffersCompute.clear();
	}

	// Return a command buffer for the current frame index
	void Renderer::beginFrame()
	{
		assert(!isFrameStarted && "Can't call beginFrame while already in progress.");

		/* wait for both fences before getting the new framebuffer image */
		std::vector<VkFence> waitFences = { renderData.rdComputeFence[currentFrameIndex], renderData.rdRenderFence[currentFrameIndex] };
		VkResult result = vkWaitForFences(
			engineDevice.device(),
			static_cast<uint32_t>(waitFences.size()),
			waitFences.data(),
			VK_TRUE,
			UINT64_MAX
		);

		if (result != VK_SUCCESS) {
			std::printf("%s error: waiting for fences failed (error: %i)\n", __FUNCTION__, result);
			throw std::runtime_error("waiting for fences failed");
		}
		//currentImageIndex = 0;
		// Acquire an image from the swap chain
		result = vkAcquireNextImageKHR(
			engineDevice.device(),
			aveng_swapchain->getSwapchain(),
			UINT64_MAX, //std::numeric_limits<uint64_t>::max(), // Is this more portable?
			renderData.rdPresentSemaphore[currentFrameIndex],  // Can be an unsignaled semaphore, fence, or both
			VK_NULL_HANDLE,
			&currentImageIndex
		);

		// This error will occur after window resize
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			recreateSwapChain();
			return;
		}
		// This could potentially occur during window resize events
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			throw std::runtime_error("Failed to acquire swap chain image.");
		}

		isFrameStarted = true;

		// This is the first point during a new frame where the command buffer is first captured.
		assert(renderData.rdCommandBuffersGraphics[currentFrameIndex] == getCurrentCommandBufferGraphics()
			&& "Incorrect Command Buffer in use");

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (vkBeginCommandBuffer(renderData.rdCommandBuffersGraphics[currentFrameIndex], &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("Command Buffer failed to begin recording.");
		}

	}

	// 
	void  Renderer::endFrame()
	{
		assert(isFrameStarted && "Can't call endFrame while frame is not in progress.");

		endSwapChainRenderPass(renderData.rdCommandBuffersGraphics[currentFrameIndex]);
		if (vkEndCommandBuffer(renderData.rdCommandBuffersGraphics[currentFrameIndex]) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to record command buffer.");
		}

		// Submit to graphics queue while handling cpu and gpu sync, executing the command buffers. NOTE: Why are we passing the image index in a pointer?
		//auto result = aveng_swapchain->submitCommandBuffers(&commandBuffer, &currentImageIndex); DEPRECATED

		/* submit command buffer */
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		std::vector<VkSemaphore> waitSemaphores = { renderData.rdComputeSemaphore[currentFrameIndex], renderData.rdPresentSemaphore[currentFrameIndex]};
		std::vector<VkPipelineStageFlags> waitStages = { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		/* compute shader: contine if in vertex input ready
		 * vertex shader: wait for color attachment output ready */
		submitInfo.pWaitDstStageMask = waitStages.data();

		submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		submitInfo.pWaitSemaphores = waitSemaphores.data();

		std::vector<VkSemaphore> signalSemaphores = { renderData.rdRenderSemaphore[currentFrameIndex], renderData.rdGraphicSemaphore[currentFrameIndex]};

		submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
		submitInfo.pSignalSemaphores = signalSemaphores.data();

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &renderData.rdCommandBuffersGraphics[currentFrameIndex];

		result = vkQueueSubmit(engineDevice.graphicsQueue(), 1, &submitInfo, renderData.rdRenderFence[currentFrameIndex]);
		if (result != VK_SUCCESS) {
			std::printf("%s error: failed to submit draw command buffer (%i)\n", __FUNCTION__, result);
			throw std::runtime_error("failed to submit draw command buffer");
		}

		/* trigger swapchain image presentation */
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &renderData.rdRenderSemaphore[currentFrameIndex];

		VkSwapchainKHR swapchain = aveng_swapchain->getSwapchain();

		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;

		presentInfo.pImageIndices = &currentImageIndex;

		result = vkQueuePresentKHR(engineDevice.presentQueue(), &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || aveng_window.wasWindowResized())
		{
			/*
			* Possible TODO: `vkResetFences(device, 1, &inFlightFences[currentFrame]);` if VK_ERROR_OUT_OF_DATE_KHR
			* Forgetting to do this could create a deadlock
			*/
			aveng_window.resetWindowResizedFlag();
			recreateSwapChain();
		}
		else if (result != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to present swap chain image.");
		}

		isFrameStarted = false;
		// Advance to the next image
		currentFrameIndex = (currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;

	}


	void  Renderer::beginSwapChainRenderPass()
	{

		assert(isFrameStarted && "Can't call beginSwapChain if frame is not in progress.");
		assert(
			renderData.rdCommandBuffersGraphics[currentFrameIndex] == getCurrentCommandBufferGraphics() &&
			"Can't begin render pass on command buffer from a different frame");

		// Clear Color for now
		glm::vec3 rgb = glm::vec3(0.001f, 0.008f, 0.06f); // Cool, dark midnight blue

		// Render pass info
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = aveng_swapchain->getRenderPass();
		renderPassInfo.framebuffer = aveng_swapchain->getFrameBuffer(currentImageIndex);

		// The area where shader loading and storing takes place.
		renderPassInfo.renderArea.offset = { 0,0 };
		renderPassInfo.renderArea.extent = aveng_swapchain->getSwapChainExtent();

		std::array<VkClearValue, 2> clearValues{};
		clearValues[0].color = { rgb.r, rgb.g, rgb.b, 1.0f };
		clearValues[1].depthStencil = { 1.0f, 0 };
		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		// VK_SUBPASS_CONTENTS_INLINE signals that subsequent renderpass commands come directly from the primary command buffer.
		// No secondary buffers are currently being utilized.
		// For this reason we cannot Mix both Inline command buffers AND secondary command buffers in this render pass's execution.
		vkCmdBeginRenderPass(renderData.rdCommandBuffersGraphics[currentFrameIndex], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Configure the viewport and scissor
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(aveng_swapchain->getSwapChainExtent().width);
		viewport.height = static_cast<float>(aveng_swapchain->getSwapChainExtent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{ {0, 0}, aveng_swapchain->getSwapChainExtent() };
		vkCmdSetViewport(renderData.rdCommandBuffersGraphics[currentFrameIndex], 0, 1, &viewport);
		vkCmdSetScissor(renderData.rdCommandBuffersGraphics[currentFrameIndex], 0, 1, &scissor);

	}

	void Renderer::endSwapChainRenderPass(VkCommandBuffer _commandBufferGraphics)
	{
		assert(isFrameStarted && "Can't call endSwapChain if frame is not in progress.");
		assert(
			_commandBufferGraphics == getCurrentCommandBufferGraphics() &&
			"Can't end render pass on command buffer from a different frame");
		vkCmdEndRenderPass(_commandBufferGraphics);
	}
	 
	void Renderer::setupDescriptors()
	{

		// int numObjects = sceneLoader.getObjectCount();

		// Create Descriptor Pools using dynamic texture count
		renderData.avengDescriptorPool = AvengDescriptorPool::Builder(engineDevice)
			.setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * 2000)  // Increased for animation descriptor sets
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 500)
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * 500)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, SwapChain::MAX_FRAMES_IN_FLIGHT * 500)
			.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 500)  // NEW: For animation SSBOs
			.build();

		// Define buffer vec's that are managed by the Renderer
		mPerspectiveViewMatrixUBOBuffers	= std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderModelRootMatrixBuffers		= std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mNodeTransformBuffers				= std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderTrsMatrixBuffers				= std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderBoneMatrixBuffers			= std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mLightDataBuffers					= std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT);

		// Define descriptor set vec's
		renderData.rdAvengDescriptorSets = std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);
		renderData.rdAvengAnimationDescriptorSets = std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);
		renderData.rdAvengComputeTransformDescriptorSets = std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);
		renderData.rdAvengComputeMatrixMultDescriptorSets = std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);
		renderData.basicLightingDescriptorSets = std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);

		// Create Buffers using VMA
		// VMA_MEMORY_USAGE_AUTO: Let VMA choose optimal memory type
		// VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT: CPU will write to this buffer frequently  
		// VMA_ALLOCATION_CREATE_MAPPED_BIT: Keep buffer persistently mapped for performance

		size_t bufferSize = 1024; // TODO?

		// Renderer::draw
		for (int i = 0; i < mPerspectiveViewMatrixUBOBuffers.size(); i++) {
			mPerspectiveViewMatrixUBOBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				bufferSize, // sizeof(VkUploadMatrices) perhaps
				1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mPerspectiveViewMatrixUBOBuffers[i]->map();
		}

		// Renderer::draw
		for (int i = 0; i < mNodeTransformBuffers.size(); i++) {
			mNodeTransformBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				// sizeof(NodeTransformData), 
				bufferSize,
				1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mNodeTransformBuffers[i]->map();
		}

		// Renderer::draw
		for (int i = 0; i < mShaderModelRootMatrixBuffers.size(); i++) {
			mShaderModelRootMatrixBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				bufferSize, 
				1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mShaderModelRootMatrixBuffers[i]->map();
		}

		for (int i = 0; i < mShaderTrsMatrixBuffers.size(); i++) {
			mShaderTrsMatrixBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				bufferSize, 
				1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mShaderTrsMatrixBuffers[i]->map();
		}

		for (int i = 0; i < mShaderBoneMatrixBuffers.size(); i++) {
			mShaderBoneMatrixBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				bufferSize, 
				1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mShaderBoneMatrixBuffers[i]->map();
		}

		// Renderer::draw but could move to the light system
		for (int i = 0; i < mLightDataBuffers.size(); i++) {
			mLightDataBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(LightsUbo), 
				1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mLightDataBuffers[i]->map();
		}

		// A Spare for a Dynamic UBO Buffer
		//for (int i = 0; i < u_ObjBuffers.size(); i++) {
		//	u_ObjBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
		//		calculateDynamicUBOStride(), numObjects, // HARDCODED - Dynamic UBO size
		//		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		//		VMA_MEMORY_USAGE_AUTO,
		//		calculateDynamicUBOStride(), // minOffsetAlignment
		//		VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
		//	u_ObjBuffers[i]->map();
		//}

		// Check memory budget after buffer creation
		engineDevice.printMemoryStats();
		if (engineDevice.isMemoryPressureHigh()) {
			std::cout << "[WARNING] High memory pressure detected!" << std::endl;
		}

		/**
		* Note: We can define all of the descriptor set layouts here, but when binding them,
		* we do it from the location that's ultimately responsible for their data.
		* 
		* For example, every model needs to write
		* 
		* Therefore, below are the descriptor set layouts used engine-wide
		*/

		/* imageViews */
		renderData.rdAvengTextureDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // Texture count is 1 for now
			.build();

		/* non-animated shader */
		renderData.rdAvengBasicDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1) // Perspective/View/AmbientLight Matrix UBO
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1) // Model Root Matrix SSBO (worldPos) Can this be a Dynamic UBO for perf increase??
			.build();

		/* animated shader */
		renderData.rdAvengAnimationDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1) // Perspective/View Matrix UBO
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1) // Bone Matrix SSBO
			.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1) // Model Root Matrix SSBO (worldPos) Can this be a Dynamic UBO for perf increase??
			.build();

		/* compute transformation shader */
		renderData.rdAvengComputeTransformDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1) // Node Transform SSBO
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1) // TRS Matrix SSBO
			.build();

		/* compute matrix multiplication shader, global data */
		renderData.rdAvengComputeMatrixMultDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1) // TRS Matrix SSBO
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1) // Node Matrix SSBO
			.build();

		/* compute matrix multiplication shader, per-model data */
		renderData.rdAvengComputeMatrixMultPerModelDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1) // Bone Parent SSBO		 -- mShaderBoneParentBuffer.buffer
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1) // Bone Matrix Offset SSBO -- mShaderBoneMatrixOffsetBuffer.buffer
			.build();

		renderData.rdAvengBasicLightingDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1) // Light Data UBO
			.build();


		// Initial update for all graphics pipeline descriptor sets
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			auto perspectiveViewBufferInfo = mPerspectiveViewMatrixUBOBuffers[i]->descriptorInfo(sizeof(VkUploadMatrices), 0); // TODO?
			auto modelRootBufferInfo = mShaderModelRootMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0); // TODO
			auto shaderBoneMatrixInfo = mShaderBoneMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0); // TODO

			// Basic Shader
			AvengDescriptorSetWriter(*renderData.rdAvengBasicDescriptorLayout, *renderData.avengDescriptorPool)
				.writeBuffer(0, &perspectiveViewBufferInfo)
				.writeBuffer(1, &modelRootBufferInfo)
				.build(renderData.rdAvengDescriptorSets[i]);

			// Animation Shader
			AvengDescriptorSetWriter(*renderData.rdAvengAnimationDescriptorLayout, *renderData.avengDescriptorPool)
				.writeBuffer(0, &perspectiveViewBufferInfo)
				.writeBuffer(1, &shaderBoneMatrixInfo)
				.writeBuffer(2, &modelRootBufferInfo)
				.build(renderData.rdAvengAnimationDescriptorSets[i]);

			// Reference if we decide to use a dynamic UBO
			// auto objBufferInfo = u_ObjBuffers[i]->descriptorInfo(calculateDynamicUBOStride(), 0);
		}
		// updateDescriptorSets();

		// Initial update for all compute descriptor sets
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			auto nodeTransformInfo = mNodeTransformBuffers[i]->descriptorInfo(sizeof(NodeTransformData), 0);
			auto trsMatrixinfo = mShaderTrsMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0);
			auto shaderBoneMatrixInfo = mShaderBoneMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0);

			// Node Compute
			AvengDescriptorSetWriter(*renderData.rdAvengComputeTransformDescriptorLayout, *renderData.avengDescriptorPool)
				.writeBuffer(0, &nodeTransformInfo)
				.writeBuffer(1, &trsMatrixinfo)
				.build(renderData.rdAvengComputeTransformDescriptorSets[i]);

			AvengDescriptorSetWriter(*renderData.rdAvengComputeMatrixMultDescriptorLayout, *renderData.avengDescriptorPool)
				.writeBuffer(0, &trsMatrixinfo)
				.writeBuffer(1, &shaderBoneMatrixInfo)
				.build(renderData.rdAvengComputeMatrixMultDescriptorSets[i]);

		}
		// updateComputeDescriptorSets();
		
		// TODO
		// updateLightingDescriptorSets();
	
	}

	void Renderer::initializePointLightSystem()
	{
		if (!renderData.rdAvengBasicLightingDescriptorLayout) {
			throw std::runtime_error("Descriptor set layouts must be created before initializing PointLightSystem (call setupDescriptors first)");
		}

		std::cout << "Initializing PointLightSystem" << std::endl;

		// Initialize point light system using existing descriptor set layouts
		pointLightSystem.initialize(getSwapChainRenderPass());

		std::cout << "PointLightSystem initialized" << std::endl;
	}

	void Renderer::renderLights()
	{
		if (u_LightsData.numLights <= 0) {
			return; // Nothing to render
		}
		assert(renderData.rdCommandBuffersGraphics[currentFrameIndex] == getCurrentCommandBufferGraphics()
			&& "Point Light system is using the wrong command buffer");

		pointLightSystem.getPipeline()->bind(renderData.rdCommandBuffersGraphics[currentFrameIndex]);

		// vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdAvengPipeline);

		// Bind both descriptor sets
		VkDescriptorSet descriptorSets[2] = { renderData.rdAvengDescriptorSets[currentFrameIndex], renderData.basicLightingDescriptorSets[currentFrameIndex] };
		vkCmdBindDescriptorSets(
			renderData.rdCommandBuffersGraphics[currentFrameIndex],
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pointLightSystem.getPipelineLayout(),
			0,
			2, // binding 2 descriptor sets
			descriptorSets,
			0,
			nullptr);

		// Use instanced rendering: 6 vertices per light, numLights instances
		vkCmdDraw(renderData.rdCommandBuffersGraphics[currentFrameIndex], 6, u_LightsData.numLights, 0, 0);
	}

	void Renderer::addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius)
	{
		if (u_LightsData.numLights >= LightsUbo::MAX_LIGHTS) {
			std::cout << "Warning: Maximum number of lights (" << LightsUbo::MAX_LIGHTS << ") reached. Cannot add more lights." << std::endl;
			return;
		}

		u_LightsData.lightPositions[u_LightsData.numLights] = glm::vec4(position, radius);
		u_LightsData.lightColors[u_LightsData.numLights] = glm::vec4(color, intensity);
		u_LightsData.numLights++;

		gameData.numPointLights++;
		
	}

	void Renderer::clearLights()
	{
		gameData.numPointLights = 0;
		u_LightsData.numLights = 0;
		// Zero out the light arrays for clean state
		memset(u_LightsData.lightPositions, 0, sizeof(u_LightsData.lightPositions));
		memset(u_LightsData.lightColors, 0, sizeof(u_LightsData.lightColors));
	}

	bool Renderer::createPipelineLayouts() {
		/* non-animated model */
		std::vector<VkDescriptorSetLayout> layouts = {
		  renderData.rdAvengTextureDescriptorLayout->getDescriptorSetLayout(),
		  renderData.rdAvengBasicDescriptorLayout->getDescriptorSetLayout()};

		std::vector<VkPushConstantRange> pushConstants = { { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkPushConstants) } };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengPipelineLayout, layouts, pushConstants)) {
			std::printf("%s error: could not init Assimp pipeline layout\n", __FUNCTION__);
			return false;
		}

		/* animated model, needs push constant */
		std::vector<VkDescriptorSetLayout> skinningLayouts = {
		  renderData.rdAvengTextureDescriptorLayout->getDescriptorSetLayout(),
		  renderData.rdAvengAnimationDescriptorLayout->getDescriptorSetLayout() };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengAnimationPipelineLayout, skinningLayouts, pushConstants)) {
			std::printf("%s error: could not init Assimp Skinning pipeline layout\n", __FUNCTION__);
			return false;
		}

		/* transform compute */
		std::vector<VkPushConstantRange> computePushConstants = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VkComputePushConstants) } };

		std::vector<VkDescriptorSetLayout> transformLayouts = {
		  renderData.rdAvengComputeTransformDescriptorLayout->getDescriptorSetLayout() };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengComputeTransformPipelineLayout, transformLayouts, computePushConstants)) {
			std::printf("%s error: could not init Assimp transform compute pipeline layout\n", __FUNCTION__);
			return false;
		}

		/* matrix mult compute */
		std::vector<VkDescriptorSetLayout> matrixMultLayouts = {
		  renderData.rdAvengComputeMatrixMultDescriptorLayout->getDescriptorSetLayout(),
		  renderData.rdAvengComputeMatrixMultPerModelDescriptorLayout->getDescriptorSetLayout() };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengComputeMatrixMultPipelineLayout, matrixMultLayouts, computePushConstants)) {
			std::printf("%s error: could not init Assimp matrix multiplication compute pipeline layout\n", __FUNCTION__);
			return false;
		}

		return true;
	}

	bool Renderer::createPipelines() {

		VkRenderPass renderPass = aveng_swapchain->getRenderPass();

		std::string vertexShaderFile = "shaders/assimp.vert.spv";
		std::string fragmentShaderFile = "shaders/assimp.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderPass, renderData.rdAvengPipelineLayout,
			renderData.rdAvengPipeline, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp shader pipeline\n", __FUNCTION__);
			return false;
		}

		vertexShaderFile = "shaders/assimp_skinning.vert.spv";
		fragmentShaderFile = "shaders/assimp_skinning.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderPass, renderData.rdAvengAnimationPipelineLayout,
			renderData.rdAvengAnimationPipeline, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp Skinning shader pipeline\n", __FUNCTION__);
			return false;
		}

		std::string computeShaderFile = "shaders/assimp_instance_transform.comp.spv";
		if (!ComputePipeline::init(engineDevice, renderData.rdAvengComputeTransformPipelineLayout,
			renderData.rdAvengComputeTransformPipeline, computeShaderFile)) {
			std::printf("%s error: could not init Assimp Transform compute shader pipeline\n", __FUNCTION__);
			return false;
		}

		computeShaderFile = "shaders/assimp_instance_matrix_mult.comp.spv";
		if (!ComputePipeline::init(engineDevice, renderData.rdAvengComputeMatrixMultPipelineLayout,
			renderData.rdAvengComputeMatrixMultPipeline, computeShaderFile)) {
			std::printf("%s error: could not init Assimp Matrix Mult compute shader pipeline\n", __FUNCTION__);
			return false;
		}

		return true;
	}


	size_t Renderer::calculateDynamicUBOStride() const
	{
		size_t objectSize = sizeof(ObjectUniformData);
		size_t minAlignment = engineDevice.properties.limits.minUniformBufferOffsetAlignment;
		return ((objectSize + minAlignment - 1) / minAlignment) * minAlignment;
	}

	void Renderer::renderEditor() {
		auto commandBufferGfx = getCurrentCommandBufferGraphics();
		editor.render(commandBufferGfx);
	}

	void Renderer::runComputeShaders(std::shared_ptr<AvengModel> model, int numInstances, uint32_t modelOffset) {
		uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());
		VkCommandBuffer computeCommandBuffer = getCurrentCommandBufferCompute();

		/* node transformation */
		vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeTransformPipeline);
		vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeTransformPipelineLayout, 0, 1, &renderData.rdAvengComputeTransformDescriptorSets[currentFrameIndex], 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(computeCommandBuffer, renderData.rdAvengComputeTransformPipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &mComputeModelData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		vkCmdDispatch(computeCommandBuffer, numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

		/* memroy barrier between the compute shaders
		 * wait for TRS buffer to be written  */
		VkBufferMemoryBarrier trsBufferBarrier{};
		trsBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		trsBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		trsBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		trsBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		trsBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		trsBufferBarrier.buffer = mShaderTrsMatrixBuffers[currentFrameIndex]->getBuffer();
		trsBufferBarrier.offset = 0;
		trsBufferBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(computeCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
			&trsBufferBarrier, 0, nullptr);

		/* matrix multiplication */
		vkCmdBindPipeline(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeMatrixMultPipeline);

		VkDescriptorSet& modelDescriptorSet = model->getMatrixMultDescriptorSet(currentFrameIndex);
		std::vector<VkDescriptorSet> computeSets = { renderData.rdAvengComputeMatrixMultDescriptorSets[currentFrameIndex], modelDescriptorSet };
		vkCmdBindDescriptorSets(computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(computeSets.size()), computeSets.data(), 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(computeCommandBuffer, renderData.rdAvengComputeMatrixMultPipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &mComputeModelData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		vkCmdDispatch (computeCommandBuffer, numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

		/* memroy barrier after compute shader
		 * wait for bone matrix buffer to be written  */
		VkBufferMemoryBarrier boneMatrixBufferBarrier{};
		boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		boneMatrixBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		boneMatrixBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		boneMatrixBufferBarrier.buffer = mShaderBoneMatrixBuffers[currentFrameIndex]->getBuffer();
		boneMatrixBufferBarrier.offset = 0;
		boneMatrixBufferBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(getCurrentCommandBufferCompute(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
			&boneMatrixBufferBarrier, 0, nullptr);
	}

	bool Renderer::draw(float deltaTime) {
		/* no update on zero diff */
		if (deltaTime == 0.0f) {
			return true;
		}

		renderData.rdFrameTime = mFrameTimer.stop();
		mFrameTimer.start();

		/* reset timers and other values */
		renderData.rdMatricesSize = 0;
		renderData.rdUploadToUBOTime = 0.0f;
		renderData.rdUploadToVBOTime = 0.0f;
		renderData.rdMatrixGenerateTime = 0.0f;
		renderData.rdUIGenerateTime = 0.0f;

		beginFrame();

		/* calculate the size of the node matrix buffer over all animated instances */
		size_t boneMatrixBufferSize = 0;
		for (const auto& modelType : mModelInstanceData.miAssimpInstancesPerModel) {
			size_t numberOfInstances = modelType.second.size();
			std::shared_ptr<AvengModel> model = modelType.second.at(0)->getModel();
			if (numberOfInstances > 0 && model->getTriangleCount() > 0) {

				/* animated models */
				if (model->hasAnimations() && !model->getBoneList().empty()) {
					size_t numberOfBones = model->getBoneList().size();

					/* buffer size must always be a multiple of "local_size_y" instances to avoid undefined behavior */
					boneMatrixBufferSize += numberOfBones * ((numberOfInstances - 1) / 32 + 1) * 32;
				}
			}
		}

		/* clear and resize world pos matrices */
		mWorldPosMatrices.clear();
		mWorldPosMatrices.resize(mModelInstanceData.miAssimpInstances.size());
		mNodeTransFormData.clear();
		mNodeTransFormData.resize(boneMatrixBufferSize);

		/* we need to track the presence of animated models */
		bool animatedModelLoaded = false;
		size_t instanceToStore = 0;
		size_t animatedInstancesToStore = 0; // This will be the total number of bones across all model instances.

		for (const auto& modelType : mModelInstanceData.miAssimpInstancesPerModel) { //
			size_t numberOfInstances = modelType.second.size(); // second is the vector of <shared_ptr> AssimpInstance
			if (numberOfInstances > 0) {
				std::shared_ptr<AvengModel> model = modelType.second.at(0)->getModel();

				/* animated models */
				if (model->hasAnimations() && !model->getBoneList().empty()) {

					// Collect the number of bones
					size_t numberOfBones = model->getBoneList().size();
					animatedModelLoaded = true; // designate

					mMatrixGenerateTimer.start();

					// For each instance
					for (unsigned int i = 0; i < numberOfInstances; ++i) {
						// Update its animation -- Q: why are we using at() instead of indexing?? Less efficient
						modelType.second.at(i)->updateAnimation(deltaTime);
						std::vector<NodeTransformData> instanceNodeTransform = modelType.second.at(i)->getNodeTransformData();

						// Copy the NodeTransform Data to the vector of NodeTransform datas. I didn't know you can use arithmetic with iterator access patterns
						// STORAGE BUFFER DATA - Packed with every instance's data
						std::copy(instanceNodeTransform.begin(), instanceNodeTransform.end(), mNodeTransFormData.begin() + animatedInstancesToStore + i * numberOfBones);

						// STORAGE BUFFER DATA - Packed with every instance's data
						mWorldPosMatrices.at(instanceToStore + i) = modelType.second.at(i)->getWorldTransformMatrix(); // model Root Matrix SSBO data 
					}

					size_t trsMatrixSize = numberOfBones * numberOfInstances * sizeof(glm::mat4); // CPU miss

					renderData.rdMatrixGenerateTime += mMatrixGenerateTimer.stop();
					renderData.rdMatricesSize += trsMatrixSize;

					instanceToStore += numberOfInstances;
					animatedInstancesToStore += numberOfInstances * numberOfBones;
				}
				else {
					/* non-animated models */
					mMatrixGenerateTimer.start();

					for (unsigned int i = 0; i < numberOfInstances; ++i) {
						mWorldPosMatrices.at(instanceToStore + i) = modelType.second.at(i)->getWorldTransformMatrix(); // model Root Matrix SSBO data 
					}

					renderData.rdMatrixGenerateTime += mMatrixGenerateTimer.stop();
					renderData.rdMatricesSize += numberOfInstances * sizeof(glm::mat4);

					instanceToStore += numberOfInstances;
				}
			}
		}

		/* we need to update descriptors after the upload if buffer size changed */
		bool bufferResized = false;
		mUploadToUBOTimer.start();

		// Ship the SSBO data
		bufferResized = ShaderStorageBuffer::uploadSsboData(renderData, mNodeTransformBuffers[currentFrameIndex], mNodeTransFormData);
		
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		/* resize SSBO if needed */
		bufferResized |= ShaderStorageBuffer::checkForResize(renderData, mShaderTrsMatrixBuffers[currentFrameIndex], boneMatrixBufferSize * mShaderTrsMatrixBuffers[currentFrameIndex]->getAlignmentSize());// sizeof(glm::mat4));
		bufferResized |= ShaderStorageBuffer::checkForResize(renderData, mShaderBoneMatrixBuffers[currentFrameIndex], boneMatrixBufferSize * mShaderBoneMatrixBuffers[currentFrameIndex]->getAlignmentSize());// sizeof(glm::mat4));

		// Note: this occurs if ANY buffer has a new size
		if (bufferResized) {
			updateDescriptorSets();
			updateComputeDescriptorSets();
		}

		/* record compute commands */
		result = vkResetFences(engineDevice.device(), 1, &renderData.rdComputeFence[currentFrameIndex]);
		if (result != VK_SUCCESS) {
			std::printf("%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
			return false;
		}

		if (animatedModelLoaded) {

			// computeCommandBuffer = getCurrentCommandBufferCompute();

			if (!engineDevice.resetCommandBuffer(renderData.rdCommandBuffersCompute[currentFrameIndex], 0)) {
				::printf("%s error: failed to reset compute command buffer\n", __FUNCTION__);
				return false;
			}

			if (!engineDevice.beginSingleShotCommand(renderData.rdCommandBuffersCompute[currentFrameIndex])) {
				std::printf("%s error: failed to begin compute command buffer\n", __FUNCTION__);
				return false;
			}

			uint32_t computeShaderModelOffset = 0;
			for (const auto& modelType : mModelInstanceData.miAssimpInstancesPerModel) {
				size_t numberOfInstances = modelType.second.size();
				std::shared_ptr<AvengModel> model = modelType.second.at(0)->getModel();
				if (numberOfInstances > 0 && model->getTriangleCount() > 0) {

					/* compute shader for animated models only */
					if (model->hasAnimations() && !model->getBoneList().empty()) {
						size_t numberOfBones = model->getBoneList().size();

						runComputeShaders(model, numberOfInstances, computeShaderModelOffset);

						computeShaderModelOffset += numberOfInstances * numberOfBones;
					}
				}
			}

			if (!engineDevice.endCommandBuffer(renderData.rdCommandBuffersCompute[currentFrameIndex])) {
				std::printf("%s error: failed to end compute command buffer\n", __FUNCTION__);
				return false;
			}

			/* submit compute commands */
			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			VkSubmitInfo computeSubmitInfo{};
			computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			computeSubmitInfo.commandBufferCount = 1;
			computeSubmitInfo.pCommandBuffers = &renderData.rdCommandBuffersCompute[currentFrameIndex];
			computeSubmitInfo.signalSemaphoreCount = 1;
			computeSubmitInfo.pSignalSemaphores = &renderData.rdComputeSemaphore[currentFrameIndex];
			computeSubmitInfo.waitSemaphoreCount = 1;
			computeSubmitInfo.pWaitSemaphores = &renderData.rdGraphicSemaphore[currentFrameIndex];
			computeSubmitInfo.pWaitDstStageMask = &waitStage;

			result = vkQueueSubmit(engineDevice.computeQueue(), 1, &computeSubmitInfo, renderData.rdComputeFence[currentFrameIndex]);
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
				return false;
			};
		}
		else {
			/* do an empty submit if we don't have animated models to satisfy fence and semaphor */
			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			VkSubmitInfo computeSubmitInfo{};
			computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			computeSubmitInfo.signalSemaphoreCount = 1;
			computeSubmitInfo.pSignalSemaphores = &renderData.rdComputeSemaphore[currentFrameIndex];
			computeSubmitInfo.waitSemaphoreCount = 1;
			computeSubmitInfo.pWaitSemaphores = &renderData.rdGraphicSemaphore[currentFrameIndex];
			computeSubmitInfo.pWaitDstStageMask = &waitStage;

			// No Validation errors on first frame
			result = vkQueueSubmit(engineDevice.computeQueue(), 1, &computeSubmitInfo, renderData.rdComputeFence[currentFrameIndex]);
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
				return false;
			};
		}

		// handleMovementKeys();

		/* we need to update descriptors after the upload if buffer size changed */
		mUploadToUBOTimer.start();
		UniformBuffer::uploadData(mPerspectiveViewMatrixUBOBuffers[currentFrameIndex], &mMatrices);
		bufferResized = ShaderStorageBuffer::uploadSsboData(renderData, mShaderModelRootMatrixBuffers[currentFrameIndex], mWorldPosMatrices);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		if (bufferResized) {
			updateDescriptorSets();
			updateComputeDescriptorSets();
		}

		/* start with graphics rendering */
		result = vkResetFences(engineDevice.device(), 1, &renderData.rdRenderFence[currentFrameIndex]);
		if (result != VK_SUCCESS) {
			std::printf("%s error:  fence reset failed (error: %i)\n", __FUNCTION__, result);
			return false;
		}

		// NOTE: This would destroy the current renderpass (if there were one)
		if (!engineDevice.resetCommandBuffer(renderData.rdCommandBuffersGraphics[currentFrameIndex], 0)) {
			std::printf("%s error: failed to reset command buffer\n", __FUNCTION__);
			return false;
		}

		////setupCommandBuffer = engineDevice.beginSingleTimeCommands()

		///**
		//* [!][!]
		//* [!][!]
		//* [!][!]
		//* 
		//* NOTE TO SELF: If sh*t hits the fan, make sure you're not mixing up single shot commands with the 
		//* primary use-case of them
		//* 
		//* [!][!]
		//* [!][!]
		//* [!][!]
		//*/

		if (!engineDevice.beginSingleShotCommand(renderData.rdCommandBuffersGraphics[currentFrameIndex])) {
			std::printf("%s error: failed to begin command buffer\n", __FUNCTION__);
			return false;
		}

		beginSwapChainRenderPass();

		/* draw the models */
		uint32_t worldPosOffset = 0;
		uint32_t skinMatOffset = 0;
		for (const auto& modelType : mModelInstanceData.miAssimpInstancesPerModel) {
			size_t numberOfInstances = modelType.second.size();
			std::shared_ptr<AvengModel> model = modelType.second.at(0)->getModel();

			if (numberOfInstances > 0 && model->getTriangleCount() > 0) {

				/* animated models */
				if (model->hasAnimations() && !model->getBoneList().empty()) {
					uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());

					vkCmdBindPipeline(renderData.rdCommandBuffersGraphics[currentFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdAvengAnimationPipeline);

					vkCmdBindDescriptorSets(renderData.rdCommandBuffersGraphics[currentFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
						renderData.rdAvengAnimationPipelineLayout, 1, 1, &renderData.rdAvengAnimationDescriptorSets[currentFrameIndex], 0, nullptr);

					mUploadToUBOTimer.start();
					mModelData.pkModelStride = numberOfBones;
					mModelData.pkWorldPosOffset = worldPosOffset;
					mModelData.pkSkinMatOffset = skinMatOffset;
					vkCmdPushConstants(renderData.rdCommandBuffersGraphics[currentFrameIndex], renderData.rdAvengAnimationPipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mModelData);
					renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

					model->drawInstancedV2(renderData, numberOfInstances, currentFrameIndex);

					worldPosOffset += numberOfInstances;
					skinMatOffset += numberOfInstances * numberOfBones;
				}
				else {
					/* non-animated models */

					vkCmdBindPipeline(renderData.rdCommandBuffersGraphics[currentFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdAvengPipeline);

					vkCmdBindDescriptorSets(renderData.rdCommandBuffersGraphics[currentFrameIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
						renderData.rdAvengPipelineLayout, 1, 1, &renderData.rdAvengDescriptorSets[currentFrameIndex], 0, nullptr);

					mUploadToUBOTimer.start();
					mModelData.pkModelStride = 0;
					mModelData.pkWorldPosOffset = worldPosOffset;
					mModelData.pkSkinMatOffset = 0;
					vkCmdPushConstants(renderData.rdCommandBuffersGraphics[currentFrameIndex], renderData.rdAvengPipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mModelData);
					renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

					model->drawInstancedV2(renderData, numberOfInstances, currentFrameIndex);

					worldPosOffset += numberOfInstances;
				}
			}
		}

		//renderLights();

#ifdef ENABLE_EDITOR
		renderEditor();
#endif

		endFrame();

		return true;
	}

	void Renderer::updateDescriptorSets() {

		int i = currentFrameIndex;

		auto perspectiveViewBufferInfo = mPerspectiveViewMatrixUBOBuffers[i]->descriptorInfo(sizeof(VkUploadMatrices), 0); // TODO?
		auto modelRootBufferInfo = mShaderModelRootMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0); // TODO
		auto shaderBoneMatrixInfo = mShaderBoneMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0); // TODO

		// Basic Shader
		AvengDescriptorSetWriter(*renderData.rdAvengBasicDescriptorLayout, *renderData.avengDescriptorPool)
			.writeBuffer(0, &perspectiveViewBufferInfo)
			.writeBuffer(1, &modelRootBufferInfo)
			.build(renderData.rdAvengDescriptorSets[i]);

		// Animation Shader
		AvengDescriptorSetWriter(*renderData.rdAvengAnimationDescriptorLayout, *renderData.avengDescriptorPool)
			.writeBuffer(0, &perspectiveViewBufferInfo)
			.writeBuffer(1, &shaderBoneMatrixInfo)
			.writeBuffer(2, &modelRootBufferInfo)
			.build(renderData.rdAvengAnimationDescriptorSets[i]);

		// Reference if we decide to use a dynamic UBO
		// auto objBufferInfo = u_ObjBuffers[i]->descriptorInfo(calculateDynamicUBOStride(), 0);
		
	}

	void Renderer::updateLightingDescriptorSets() {

		int i = currentFrameIndex;

		// TODO
		auto lightsBufferInfo = mLightDataBuffers[i]->descriptorInfo(sizeof(LightsUbo), 0);
		AvengDescriptorSetWriter(*renderData.rdAvengBasicLightingDescriptorLayout, *renderData.avengDescriptorPool)
			.writeBuffer(0, &lightsBufferInfo)
			.build(renderData.basicLightingDescriptorSets[i]);
		
	}

	void Renderer::updateComputeDescriptorSets() {

		int i = currentFrameIndex;

		auto nodeTransformInfo = mNodeTransformBuffers[i]->descriptorInfo(sizeof(NodeTransformData), 0);
		auto trsMatrixinfo = mShaderTrsMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0);
		auto shaderBoneMatrixInfo = mShaderBoneMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0);

		// Node Compute
		AvengDescriptorSetWriter(*renderData.rdAvengComputeTransformDescriptorLayout, *renderData.avengDescriptorPool)
			.writeBuffer(0, &nodeTransformInfo)
			.writeBuffer(1, &trsMatrixinfo)
			.build(renderData.rdAvengComputeTransformDescriptorSets[i]);


		AvengDescriptorSetWriter(*renderData.rdAvengComputeMatrixMultDescriptorLayout, *renderData.avengDescriptorPool)
			.writeBuffer(0, &trsMatrixinfo)
			.writeBuffer(1, &shaderBoneMatrixInfo)
			.build(renderData.rdAvengComputeMatrixMultDescriptorSets[i]);

		

	}

	void Renderer::updateFrameData(const glm::mat4& projection, const glm::mat4& view)
	{
		mMatrices.projectionMatrix = projection;
		mMatrices.viewMatrix = view;
		// ambient light as well but it's static for now
	}

	bool Renderer::createSyncObjects() {

		renderData.rdPresentSemaphore.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdRenderSemaphore.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdGraphicSemaphore.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdComputeSemaphore.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		renderData.rdRenderFence.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdComputeFence.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		if (!SyncObjects::init(engineDevice, renderData, SwapChain::MAX_FRAMES_IN_FLIGHT)) {
			std::printf("%s error: could not create sync objects\n", __FUNCTION__);
			return false;
		}
		return true;
	}

	void Renderer::cleanup() {
		VkResult result = vkDeviceWaitIdle(engineDevice.device());
		if (result != VK_SUCCESS) {
			std::printf("%s fatal error: could not wait for device idle (error: %i)\n", __FUNCTION__, result);
			return;
		}

		/* delete models to destroy Vulkan objects */
		for (const auto& model : mModelInstanceData.miModelList) {
			model->cleanup(engineDevice, renderData, aveng_swapchain->MAX_FRAMES_IN_FLIGHT);
		}

		for (const auto& model : mModelInstanceData.miPendingDeleteAvengModels) {
			model->cleanup(engineDevice, renderData, aveng_swapchain->MAX_FRAMES_IN_FLIGHT);
		}

		// mUserInterface.cleanup(renderData);

		SyncObjects::cleanup(engineDevice, renderData, SwapChain::MAX_FRAMES_IN_FLIGHT);

		SkinningPipeline::cleanup(engineDevice, renderData.rdAvengPipeline);
		SkinningPipeline::cleanup(engineDevice, renderData.rdAvengAnimationPipeline);
		ComputePipeline::cleanup(engineDevice, renderData.rdAvengComputeTransformPipeline);
		ComputePipeline::cleanup(engineDevice, renderData.rdAvengComputeMatrixMultPipeline);

		PipelineLayout::cleanup(engineDevice, renderData.rdAvengPipelineLayout);
		PipelineLayout::cleanup(engineDevice, renderData.rdAvengAnimationPipelineLayout);
		PipelineLayout::cleanup(engineDevice, renderData.rdAvengComputeTransformPipelineLayout);
		PipelineLayout::cleanup(engineDevice, renderData.rdAvengComputeMatrixMultPipelineLayout);
		

		//vkFreeDescriptorSets(engineDevice.device(), renderData.rdDescriptorPool, 1, &renderData.rdAssimpDescriptorSet);
		//vkFreeDescriptorSets(engineDevice.device(), renderData.rdDescriptorPool, 1, &renderData.rdAssimpSkinningDescriptorSet);
		//vkFreeDescriptorSets(engineDevice.device(), renderData.rdDescriptorPool, 1, &renderData.rdAssimpComputeTransformDescriptorSet);
		//vkFreeDescriptorSets(engineDevice.device(), renderData.rdDescriptorPool, 1, &renderData.rdAssimpComputeMatrixMultDescriptorSet);

		//renderData.avengDescriptorPool->freeDescriptors(renderData.rdAvengDescriptorSets);
		//renderData.avengDescriptorPool->freeDescriptors(renderData.rdAvengAnimationDescriptorSets);
		//renderData.avengDescriptorPool->freeDescriptors(renderData.rdAvengComputeTransformDescriptorSets);
		//renderData.avengDescriptorPool->freeDescriptors(renderData.rdAvengComputeMatrixMultDescriptorSets);

		// TODO - Light Descriptor Cleanup, check texture descriptor cleanup, etc.

		// These should be handled by the implicit destructor
		//vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpDescriptorLayout, nullptr);
		//vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpSkinningDescriptorLayout, nullptr);
		//vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpTextureDescriptorLayout, nullptr);
		//vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeTransformDescriptorLayout, nullptr);
		//vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeMatrixMultDescriptorLayout, nullptr);
		//vkDestroyDescriptorSetLayout(renderData.rdVkbDevice.device, renderData.rdAssimpComputeMatrixMultPerModelDescriptorLayout, nullptr);
		// Ditto
		//vkDestroyDescriptorPool(renderData.rdVkbDevice.device, renderData.rdDescriptorPool, nullptr);

		std::printf("%s: Vulkan renderer destroyed\n", __FUNCTION__);
	}

} // NS