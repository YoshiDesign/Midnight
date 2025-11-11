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

		// Define buffer vec's that are managed by the Renderer
		mPerspectiveViewMatrixUBOBuffers = std::vector<VkUniformBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderModelRootMatrixBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mNodeTransformBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderTrsMatrixBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderBoneMatrixBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mLightDataBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);

		// Define descriptor set vec's
		renderData.rdAvengDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		renderData.rdAvengAnimationDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		renderData.rdAvengComputeTransformDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		renderData.rdAvengComputeMatrixMultDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		renderData.basicLightingDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

		recreateSwapChain();

		createCommandBuffers();

		if (!createMatrixUBO()) {
			throw std::runtime_error("Failed to create UBO");
		}

		if (!createSSBOs()) {
			throw std::runtime_error("Failed to create SSBOs");
		}

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
		mModelInstanceData.miModelAddCallbackFunction = [this](std::string fileName) {/* return addModel(fileName);*/ return queueModelLoad(fileName); };
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
		// vkQueueWaitIdle(engineDevice.graphicsQueue());

		mFrameTimer.start();
		std::printf("%s: Vulkan renderer initialized!\n");

	}

	Renderer::~Renderer()
	{
		std::cout << "Destroying Renderer..." << std::endl;
		freeCommandBuffers();
		cleanup();
		// vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
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
		// This check now protects against programming errors
		if (isFrameStarted) {
			std::printf("ERROR: addModel called during frame! This should never happen.\n");
			throw std::runtime_error("Internal error: model loading during frame");
		}

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

	// In Renderer.cpp:

	bool Renderer::queueModelLoad(const std::string& filepath) {
		mPendingModelLoads.push_back({ filepath });
		std::printf("Queued model load (will load after current frame)\n");
		return true;
	}

	void Renderer::processPendingModelLoads() {
		if (mPendingModelLoads.empty()) {
			return;
		}

		// Make sure no frame is in progress
		assert(!isFrameStarted && "Cannot process model loads during frame!");

		for (const auto& pending : mPendingModelLoads) {
			std::printf("Processing queued model load: %s\n", pending.filepath.c_str());
			// if (!addModel(pending.filepath)) {
			if (!addModel(pending.filepath)) {
				std::printf("Failed to load queued model: %s\n", pending.filepath.c_str());
			}
		}

		mPendingModelLoads.clear();
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

		//VkCommandBufferBeginInfo beginInfo{};
		//beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		//if (vkBeginCommandBuffer(renderData.rdCommandBuffersGraphics[currentFrameIndex], &beginInfo) != VK_SUCCESS)
		//{
		//	throw std::runtime_error("Command Buffer failed to begin recording.");
		//}

	}

	// 
	void  Renderer::endFrame()
	{
		assert(isFrameStarted && "Can't call endFrame while frame is not in progress.");

		endSwapChainRenderPass();
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

	void Renderer::endSwapChainRenderPass()
	{
		assert(isFrameStarted && "Can't call endSwapChain if frame is not in progress.");
		assert(
			renderData.rdCommandBuffersGraphics[currentFrameIndex] == getCurrentCommandBufferGraphics() &&
			"Can't end render pass on command buffer from a different frame");
		vkCmdEndRenderPass(renderData.rdCommandBuffersGraphics[currentFrameIndex]); 
	}
	 
	bool Renderer::setupDescriptors()
	{

		std::vector<VkDescriptorPoolSize> poolSizes =
		{
		  { VK_DESCRIPTOR_TYPE_SAMPLER, 10000 },
		  { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10000 },
		  { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10000 },
		  { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
		  { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
		  { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
		};

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 10000;
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		VkResult result = vkCreateDescriptorPool(engineDevice.device(), &poolInfo, nullptr, &renderData.avengDescriptorPool);
		if (result != VK_SUCCESS) {
			Logger::log(1, "%s error: could not init descriptor pool (error: %i)\n", __FUNCTION__, result);
			return false;
		}

		createDescriptorLayouts();

		createDescriptorSets();

		updateDescriptorSets(2);

		updateComputeDescriptorSets(2);
		
		// TODO
		// updateLightingDescriptorSets();
	
	}

	bool Renderer::createDescriptorLayouts() {
		VkResult result;

		{

			 /* texture */
			VkDescriptorSetLayoutBinding assimpTextureBind{};
			assimpTextureBind.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			assimpTextureBind.binding = 0;
			assimpTextureBind.descriptorCount = 1;
			assimpTextureBind.pImmutableSamplers = nullptr;
			assimpTextureBind.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpTexBindings = { assimpTextureBind };

			VkDescriptorSetLayoutCreateInfo assimpTextureCreateInfo{};
			assimpTextureCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpTextureCreateInfo.bindingCount = static_cast<uint32_t>(assimpTexBindings.size());
			assimpTextureCreateInfo.pBindings = assimpTexBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpTextureCreateInfo,
				nullptr, &renderData.rdAvengTextureDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp texture descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}

		{
			/* non-animated shader */
			VkDescriptorSetLayoutBinding assimpUboBind{};
			assimpUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpUboBind.binding = 0;
			assimpUboBind.descriptorCount = 1;
			assimpUboBind.pImmutableSamplers = nullptr;
			assimpUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSsboBind{};
			assimpSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSsboBind.binding = 1;
			assimpSsboBind.descriptorCount = 1;
			assimpSsboBind.pImmutableSamplers = nullptr;
			assimpSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpUboBind, assimpSsboBind };

			VkDescriptorSetLayoutCreateInfo assimpCreateInfo{};
			assimpCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpCreateInfo.bindingCount = static_cast<uint32_t>(assimpBindings.size());
			assimpCreateInfo.pBindings = assimpBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpCreateInfo,
				nullptr, &renderData.rdAvengDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}

		{
			/* animated shader */
			VkDescriptorSetLayoutBinding assimpUboBind{};
			assimpUboBind.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			assimpUboBind.binding = 0;
			assimpUboBind.descriptorCount = 1;
			assimpUboBind.pImmutableSamplers = nullptr;
			assimpUboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSkinningSsboBind{};
			assimpSkinningSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSkinningSsboBind.binding = 1;
			assimpSkinningSsboBind.descriptorCount = 1;
			assimpSkinningSsboBind.pImmutableSamplers = nullptr;
			assimpSkinningSsboBind.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			VkDescriptorSetLayoutBinding assimpSkinningSsboBind2{};
			assimpSkinningSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSkinningSsboBind2.binding = 2;
			assimpSkinningSsboBind2.descriptorCount = 1;
			assimpSkinningSsboBind2.pImmutableSamplers = nullptr;
			assimpSkinningSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings = { assimpUboBind, assimpSkinningSsboBind, assimpSkinningSsboBind2 };

			VkDescriptorSetLayoutCreateInfo assimpSkinningCreateInfo{};
			assimpSkinningCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpSkinningCreateInfo.bindingCount = static_cast<uint32_t>(assimpSkinningBindings.size());
			assimpSkinningCreateInfo.pBindings = assimpSkinningBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpSkinningCreateInfo,
				nullptr, &renderData.rdAvengAnimationDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp skinning buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}

		{
			/* compute transformation shader */
			VkDescriptorSetLayoutBinding assimpTransformSsboBind{};
			assimpTransformSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpTransformSsboBind.binding = 0;
			assimpTransformSsboBind.descriptorCount = 1;
			assimpTransformSsboBind.pImmutableSamplers = nullptr;
			assimpTransformSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			VkDescriptorSetLayoutBinding assimpTrsSsboBind{};
			assimpTrsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpTrsSsboBind.binding = 1;
			assimpTrsSsboBind.descriptorCount = 1;
			assimpTrsSsboBind.pImmutableSamplers = nullptr;
			assimpTrsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpTransformBindings = { assimpTransformSsboBind, assimpTrsSsboBind };

			VkDescriptorSetLayoutCreateInfo assimpTransformCreateInfo{};
			assimpTransformCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpTransformCreateInfo.bindingCount = static_cast<uint32_t>(assimpTransformBindings.size());
			assimpTransformCreateInfo.pBindings = assimpTransformBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpTransformCreateInfo,
				nullptr, &renderData.rdAvengComputeTransformDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp transform compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}

		{
			/* compute matrix multiplication shader, global data */
			VkDescriptorSetLayoutBinding assimpTrsSsboBind{};
			assimpTrsSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpTrsSsboBind.binding = 0;
			assimpTrsSsboBind.descriptorCount = 1;
			assimpTrsSsboBind.pImmutableSamplers = nullptr;
			assimpTrsSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			VkDescriptorSetLayoutBinding assimpNodeMatricesSsboBind{};
			assimpNodeMatricesSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpNodeMatricesSsboBind.binding = 1;
			assimpNodeMatricesSsboBind.descriptorCount = 1;
			assimpNodeMatricesSsboBind.pImmutableSamplers = nullptr;
			assimpNodeMatricesSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpMatMultBindings =
			{ assimpTrsSsboBind,assimpNodeMatricesSsboBind };

			VkDescriptorSetLayoutCreateInfo assimpMatrixMultCreateInfo{};
			assimpMatrixMultCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpMatrixMultCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultBindings.size());
			assimpMatrixMultCreateInfo.pBindings = assimpMatMultBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpMatrixMultCreateInfo,
				nullptr, &renderData.rdAvengComputeMatrixMultDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp matrix multiplication global compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}

		{
			/* compute matrix multiplication shader, per-model data */
			VkDescriptorSetLayoutBinding assimpParentMatrixSsboBind{};
			assimpParentMatrixSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpParentMatrixSsboBind.binding = 0;
			assimpParentMatrixSsboBind.descriptorCount = 1;
			assimpParentMatrixSsboBind.pImmutableSamplers = nullptr;
			assimpParentMatrixSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			VkDescriptorSetLayoutBinding assimpBoneOffsetSsboBind{};
			assimpBoneOffsetSsboBind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpBoneOffsetSsboBind.binding = 1;
			assimpBoneOffsetSsboBind.descriptorCount = 1;
			assimpBoneOffsetSsboBind.pImmutableSamplers = nullptr;
			assimpBoneOffsetSsboBind.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpMatMultPerModelBindings =
			{ assimpParentMatrixSsboBind, assimpBoneOffsetSsboBind };

			VkDescriptorSetLayoutCreateInfo assimpMatrixMultPerModelCreateInfo{};
			assimpMatrixMultPerModelCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			assimpMatrixMultPerModelCreateInfo.bindingCount = static_cast<uint32_t>(assimpMatMultPerModelBindings.size());
			assimpMatrixMultPerModelCreateInfo.pBindings = assimpMatMultPerModelBindings.data();

			result = vkCreateDescriptorSetLayout(engineDevice.device(), &assimpMatrixMultPerModelCreateInfo,
				nullptr, &renderData.rdAvengComputeMatrixMultPerModelDescriptorLayout);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not create Assimp matrix multiplication per model compute buffer descriptor set layout (error: %i)\n", __FUNCTION__, result);
				return false;
			}
		}

		return true;
	}

	bool Renderer::createDescriptorSets() {
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
		
			/* non-animated models */
			VkDescriptorSetAllocateInfo descriptorAllocateInfo{};
			descriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorAllocateInfo.descriptorPool = renderData.avengDescriptorPool;
			descriptorAllocateInfo.descriptorSetCount = 1;
			descriptorAllocateInfo.pSetLayouts = &renderData.rdAvengDescriptorLayout;

			VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &descriptorAllocateInfo,
				&renderData.rdAvengDescriptorSets[i]);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not allocate Assimp descriptor set (error: %i)\n", __FUNCTION__, result);
				return false;
			}

			/* animated models */
			VkDescriptorSetAllocateInfo skinningDescriptorAllocateInfo{};
			skinningDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			skinningDescriptorAllocateInfo.descriptorPool = renderData.avengDescriptorPool;
			skinningDescriptorAllocateInfo.descriptorSetCount = 1;
			skinningDescriptorAllocateInfo.pSetLayouts = &renderData.rdAvengAnimationDescriptorLayout;

			result = vkAllocateDescriptorSets(engineDevice.device(), &skinningDescriptorAllocateInfo,
				&renderData.rdAvengAnimationDescriptorSets[i]);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not allocate Assimp Skinning descriptor set (error: %i)\n", __FUNCTION__, result);
				return false;
			}

			/* compute transform */
			VkDescriptorSetAllocateInfo computeTransformDescriptorAllocateInfo{};
			computeTransformDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			computeTransformDescriptorAllocateInfo.descriptorPool = renderData.avengDescriptorPool;
			computeTransformDescriptorAllocateInfo.descriptorSetCount = 1;
			computeTransformDescriptorAllocateInfo.pSetLayouts = &renderData.rdAvengComputeTransformDescriptorLayout;

			result = vkAllocateDescriptorSets(engineDevice.device(), &computeTransformDescriptorAllocateInfo,
				&renderData.rdAvengComputeTransformDescriptorSets[i]);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not allocate Assimp Transform Compute descriptor set (error: %i)\n", __FUNCTION__, result);
				return false;
			}

			/* matrix multiplication, global data */
			VkDescriptorSetAllocateInfo computeMatrixMultDescriptorAllocateInfo{};
			computeMatrixMultDescriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			computeMatrixMultDescriptorAllocateInfo.descriptorPool = renderData.avengDescriptorPool;
			computeMatrixMultDescriptorAllocateInfo.descriptorSetCount = 1;
			computeMatrixMultDescriptorAllocateInfo.pSetLayouts = &renderData.rdAvengComputeMatrixMultDescriptorLayout;

			result = vkAllocateDescriptorSets(engineDevice.device(), &computeMatrixMultDescriptorAllocateInfo,
				&renderData.rdAvengComputeMatrixMultDescriptorSets[i]);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not allocate Assimp Matrix Mult Compute descriptor set (error: %i)\n", __FUNCTION__, result);
				return false;
			}

		}

		return true;
	}

	void Renderer::initializePointLightSystem()
	{
		//if (!renderData.rdAvengBasicLightingDescriptorLayout) {
		//	throw std::runtime_error("Descriptor set layouts must be created before initializing PointLightSystem (call setupDescriptors first)");
		//}

		//std::cout << "Initializing PointLightSystem" << std::endl;

		//// Initialize point light system using existing descriptor set layouts
		//pointLightSystem.initialize(getSwapChainRenderPass());

		//std::cout << "PointLightSystem initialized" << std::endl;
	}

	void Renderer::renderLights()
	{
		//if (u_LightsData.numLights <= 0) {
		//	return; // Nothing to render
		//}
		//assert(renderData.rdCommandBuffersGraphics[currentFrameIndex] == getCurrentCommandBufferGraphics()
		//	&& "Point Light system is using the wrong command buffer");

		//pointLightSystem.getPipeline()->bind(renderData.rdCommandBuffersGraphics[currentFrameIndex]);

		//// vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdAvengPipeline);

		//// Bind both descriptor sets
		//VkDescriptorSet descriptorSets[2] = { renderData.rdAvengDescriptorSets[currentFrameIndex], renderData.basicLightingDescriptorSets[currentFrameIndex] };
		//vkCmdBindDescriptorSets(
		//	renderData.rdCommandBuffersGraphics[currentFrameIndex],
		//	VK_PIPELINE_BIND_POINT_GRAPHICS,
		//	pointLightSystem.getPipelineLayout(),
		//	0,
		//	2, // binding 2 descriptor sets
		//	descriptorSets,
		//	0,
		//	nullptr);

		//// Use instanced rendering: 6 vertices per light, numLights instances
		//vkCmdDraw(renderData.rdCommandBuffersGraphics[currentFrameIndex], 6, u_LightsData.numLights, 0, 0);
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
		  renderData.rdAvengTextureDescriptorLayout, renderData.rdAvengDescriptorLayout};

		std::vector<VkPushConstantRange> pushConstants = { { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(VkPushConstants) } };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengPipelineLayout, layouts, pushConstants)) {
			std::printf("%s error: could not init Assimp pipeline layout\n", __FUNCTION__);
			return false;
		}

		/* animated model, needs push constant */
		std::vector<VkDescriptorSetLayout> skinningLayouts = {
		  renderData.rdAvengTextureDescriptorLayout, renderData.rdAvengAnimationDescriptorLayout };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengAnimationPipelineLayout, skinningLayouts, pushConstants)) {
			std::printf("%s error: could not init Assimp Skinning pipeline layout\n", __FUNCTION__);
			return false;
		}

		/* transform compute */
		std::vector<VkPushConstantRange> computePushConstants = { { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VkComputePushConstants) } };

		std::vector<VkDescriptorSetLayout> transformLayouts = { renderData.rdAvengComputeTransformDescriptorLayout };

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengComputeTransformPipelineLayout, transformLayouts, computePushConstants)) {
			std::printf("%s error: could not init Assimp transform compute pipeline layout\n", __FUNCTION__);
			return false;
		}

		/* matrix mult compute */
		std::vector<VkDescriptorSetLayout> matrixMultLayouts = {
		  renderData.rdAvengComputeMatrixMultDescriptorLayout, renderData.rdAvengComputeMatrixMultPerModelDescriptorLayout};

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
		editor.render(currentFrameIndex);
	}

	void Renderer::runComputeShaders(std::shared_ptr<AvengModel> model, int numInstances, uint32_t modelOffset) {
		uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());
		// VkCommandBuffer computeCommandBuffer = getCurrentCommandBufferCompute();

		/* node transformation */
		vkCmdBindPipeline(renderData.rdCommandBuffersCompute[currentFrameIndex], VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeTransformPipeline);
		std::cout << "Check [0]" << std::endl;
		vkCmdBindDescriptorSets(renderData.rdCommandBuffersCompute[currentFrameIndex], VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeTransformPipelineLayout, 0, 1, &renderData.rdAvengComputeTransformDescriptorSets[currentFrameIndex], 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(renderData.rdCommandBuffersCompute[currentFrameIndex], renderData.rdAvengComputeTransformPipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &mComputeModelData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		std::printf("Dispatching compute: %d x %d x 1\n", numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)));

		vkCmdDispatch(renderData.rdCommandBuffersCompute[currentFrameIndex], numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);
		std::printf("Compute dispatch called: bones=%d, instanceGroups=%d\n",
			numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)));

		/* memroy barrier between the compute shaders
		 * wait for TRS buffer to be written  */
		VkBufferMemoryBarrier trsBufferBarrier{};
		trsBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		trsBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		trsBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		trsBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		trsBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		trsBufferBarrier.buffer = mShaderTrsMatrixBuffers[currentFrameIndex].buffer;
		trsBufferBarrier.offset = 0;
		trsBufferBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(renderData.rdCommandBuffersCompute[currentFrameIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
			&trsBufferBarrier, 0, nullptr);

		/* matrix multiplication */
		vkCmdBindPipeline(renderData.rdCommandBuffersCompute[currentFrameIndex], VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeMatrixMultPipeline);

		VkDescriptorSet& modelDescriptorSet = model->getMatrixMultDescriptorSet(currentFrameIndex);
		std::vector<VkDescriptorSet> computeSets = { renderData.rdAvengComputeMatrixMultDescriptorSets[currentFrameIndex], modelDescriptorSet }; 
		vkCmdBindDescriptorSets(renderData.rdCommandBuffersCompute[currentFrameIndex], VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(computeSets.size()), computeSets.data(), 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(renderData.rdCommandBuffersCompute[currentFrameIndex], renderData.rdAvengComputeMatrixMultPipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &mComputeModelData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		vkCmdDispatch (renderData.rdCommandBuffersCompute[currentFrameIndex], numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

		/* memroy barrier after compute shader
		 * wait for bone matrix buffer to be written  */
		VkBufferMemoryBarrier boneMatrixBufferBarrier{};
		boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		boneMatrixBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		boneMatrixBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		boneMatrixBufferBarrier.buffer = mShaderBoneMatrixBuffers[currentFrameIndex].buffer;
		boneMatrixBufferBarrier.offset = 0;
		boneMatrixBufferBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(renderData.rdCommandBuffersCompute[currentFrameIndex], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
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
		if (!isFrameStarted) {
			std::printf("beginFrame failed/skipped (swapchain recreation), skipping frame\n");
			return true;  // Skip this frame gracefully
		}

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
					std::printf("boneMatrixBufferSize: %zu bytes", boneMatrixBufferSize);
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

						// glm::mat4 mt = modelType.second.at(i)->getWorldTransformMatrix();
						//std::cout << "INSPECTING MAT" << std::endl;
						//std::cout << glm::to_string(mt) << std::endl;
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
		bufferResized = ShaderStorageBuffer::uploadSsboData(engineDevice, mNodeTransformBuffers[currentFrameIndex], mNodeTransFormData);
		
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		/* resize SSBO if needed */
		bufferResized |= ShaderStorageBuffer::checkForResize(engineDevice, mShaderTrsMatrixBuffers[currentFrameIndex], boneMatrixBufferSize * sizeof(glm::mat4));// sizeof(glm::mat4));
		bufferResized |= ShaderStorageBuffer::checkForResize(engineDevice, mShaderBoneMatrixBuffers[currentFrameIndex], boneMatrixBufferSize * sizeof(glm::mat4));// sizeof(glm::mat4));

		// Note: this occurs if ANY buffer has a new size
		if (bufferResized) {
			std::cout << "StorageBuffer Resized - Updating Descriptor Sets" << std::endl;
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

						std::printf("[runComputeShaders] Buffer sizes - NodeTransform: %zu, TRSMatrix: %zu, BoneMatrix: %zu\n",
							mNodeTransformBuffers[currentFrameIndex].bufferSize,
							mShaderTrsMatrixBuffers[currentFrameIndex].bufferSize,
							mShaderBoneMatrixBuffers[currentFrameIndex].bufferSize);

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
		UniformBuffer::uploadData(engineDevice, mPerspectiveViewMatrixUBOBuffers[currentFrameIndex], mMatrices);
		bufferResized = ShaderStorageBuffer::uploadSsboData(engineDevice, mShaderModelRootMatrixBuffers[currentFrameIndex], mWorldPosMatrices);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		if (bufferResized) {
			std::cout << "UBO Resized - Updating Descriptor Sets" << std::endl;
			updateDescriptorSets();
			updateComputeDescriptorSets();
		}

		/* start with graphics rendering */
		result = vkResetFences(engineDevice.device(), 1, &renderData.rdRenderFence[currentFrameIndex]);
		if (result != VK_SUCCESS) {
			std::printf("%s error:  fence reset failed (error: %i)\n", __FUNCTION__, result);
			return false;
		}

		//// NOTE: This would destroy the current renderpass (if there were one)
		if (!engineDevice.resetCommandBuffer(renderData.rdCommandBuffersGraphics[currentFrameIndex], 0)) {
			std::printf("%s error: failed to reset command buffer\n", __FUNCTION__);
			return false;
		}

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

		// endFrame();
		vkCmdEndRenderPass(renderData.rdCommandBuffersGraphics[currentFrameIndex]);

		VkResult result = vkEndCommandBuffer(renderData.rdCommandBuffersGraphics[currentFrameIndex]);
		if (result != VK_SUCCESS) {
			std::printf("%s error: could not end render pass (error: %i)\n", __FUNCTION__, result);
			throw std::runtime_error("error: could not end render pass");
		}

		/* submit command buffer */
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		std::vector<VkSemaphore> waitSemaphores = { renderData.rdComputeSemaphore[currentFrameIndex], renderData.rdPresentSemaphore[currentFrameIndex] };
		std::vector<VkPipelineStageFlags> waitStages = { VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

		/* compute shader: contine if in vertex input ready
		 * vertex shader: wait for color attachment output ready */
		submitInfo.pWaitDstStageMask = waitStages.data();

		submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		submitInfo.pWaitSemaphores = waitSemaphores.data();

		std::vector<VkSemaphore> signalSemaphores = { renderData.rdRenderSemaphore[currentFrameIndex], renderData.rdGraphicSemaphore[currentFrameIndex] };

		submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
		submitInfo.pSignalSemaphores = signalSemaphores.data();

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &renderData.rdCommandBuffersGraphics[currentFrameIndex];

		result = vkQueueSubmit(engineDevice.graphicsQueue(), 1, &submitInfo, renderData.rdRenderFence[currentFrameIndex]);
		if (result != VK_SUCCESS) {
			std::printf("%s error: failed to submit draw command buffer (%i)\n", __FUNCTION__, result);
			return false;
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
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
			recreateSwapChain();
		}
		else {
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to present swapchain image\n", __FUNCTION__);
				return false;
			}
		}

		isFrameStarted = false;
		// Advance to the next image
		currentFrameIndex = (currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
		
	}

	void Renderer::updateDescriptorSets(int iters) {
		if (iters > SwapChain::MAX_FRAMES_IN_FLIGHT) {
			throw std::runtime_error("Attempting to write too many descriptor sets.");
		}
		Logger::log(1, "%s: updating descriptor sets\n", __FUNCTION__);
		for (int i = 0; i < iters; i++)
		{

			int index = i;

			// This implies we're updating descriptors during rendering. On init, we perform 2 iterations
			if (iters == 1) {
				index = currentFrameIndex;
			}
			
			/* we must update the descriptor sets whenever the buffer size has changed */
			{
				/* non-animated shader */
				VkDescriptorBufferInfo matrixInfo{};
				matrixInfo.buffer = mPerspectiveViewMatrixUBOBuffers[index].buffer;
				matrixInfo.offset = 0;
				matrixInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo worldPosInfo{};
				worldPosInfo.buffer = mShaderModelRootMatrixBuffers[index].buffer;
				worldPosInfo.offset = 0;
				worldPosInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet matrixWriteDescriptorSet{};
				matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				matrixWriteDescriptorSet.dstSet = renderData.rdAvengDescriptorSets[index];
				matrixWriteDescriptorSet.dstBinding = 0;
				matrixWriteDescriptorSet.descriptorCount = 1;
				matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

				VkWriteDescriptorSet posWriteDescriptorSet{};
				posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				posWriteDescriptorSet.dstSet = renderData.rdAvengDescriptorSets[index];
				posWriteDescriptorSet.dstBinding = 1;
				posWriteDescriptorSet.descriptorCount = 1;
				posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

				std::vector<VkWriteDescriptorSet> writeDescriptorSets =
				{ matrixWriteDescriptorSet, posWriteDescriptorSet };

				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(writeDescriptorSets.size()),
					writeDescriptorSets.data(), 0, nullptr);
			}

			{
				/* animated shader */
				VkDescriptorBufferInfo matrixInfo{};
				matrixInfo.buffer = mPerspectiveViewMatrixUBOBuffers[index].buffer;
				matrixInfo.offset = 0;
				matrixInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo boneMatrixInfo{};
				boneMatrixInfo.buffer = mShaderBoneMatrixBuffers[index].buffer;
				boneMatrixInfo.offset = 0;
				boneMatrixInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo worldPosInfo{};
				worldPosInfo.buffer = mShaderModelRootMatrixBuffers[index].buffer;
				worldPosInfo.offset = 0;
				worldPosInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet matrixWriteDescriptorSet{};
				matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				matrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationDescriptorSets[index];
				matrixWriteDescriptorSet.dstBinding = 0;
				matrixWriteDescriptorSet.descriptorCount = 1;
				matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

				VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
				boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				boneMatrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationDescriptorSets[index];
				boneMatrixWriteDescriptorSet.dstBinding = 1;
				boneMatrixWriteDescriptorSet.descriptorCount = 1;
				boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

				VkWriteDescriptorSet posWriteDescriptorSet{};
				posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				posWriteDescriptorSet.dstSet = renderData.rdAvengAnimationDescriptorSets[index];
				posWriteDescriptorSet.dstBinding = 2;
				posWriteDescriptorSet.descriptorCount = 1;
				posWriteDescriptorSet.pBufferInfo = &worldPosInfo;

				std::vector<VkWriteDescriptorSet> skinningWriteDescriptorSets =
				{ matrixWriteDescriptorSet, boneMatrixWriteDescriptorSet, posWriteDescriptorSet };

				// AVENG_DESCRIPTOR->build()
				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(skinningWriteDescriptorSets.size()),
					skinningWriteDescriptorSets.data(), 0, nullptr);
			}
		
		}

	}

	void Renderer::updateLightingDescriptorSets() {

		//int i = currentFrameIndex;

		//// TODO
		//auto lightsBufferInfo = mLightDataBuffers[i]->descriptorInfo(sizeof(LightsUbo), 0);
		//AvengDescriptorSetWriter(*renderData.rdAvengBasicLightingDescriptorLayout, *renderData.avengDescriptorPool)
		//	.writeBuffer(0, &lightsBufferInfo)
		//	.build(renderData.basicLightingDescriptorSets[i]);
		
	}

	void Renderer::updateComputeDescriptorSets(int iters) {

		Logger::log(1, "%s: updating compute descriptor sets\n", __FUNCTION__);
		for (int i = 0; i < iters; i++)
		{

			int index = i;

			// This implies we're updating descriptors during rendering. On init, we perform 2 iterations
			if (iters == 1) {
				index = currentFrameIndex;
			}

			{
				/* transform compute shader */
				VkDescriptorBufferInfo transformInfo{};
				transformInfo.buffer = mNodeTransformBuffers[index].buffer;
				transformInfo.offset = 0;
				transformInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo trsInfo{};
				trsInfo.buffer = mShaderTrsMatrixBuffers[index].buffer;
				trsInfo.offset = 0;
				trsInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet transformWriteDescriptorSet{};
				transformWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				transformWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				transformWriteDescriptorSet.dstSet = renderData.rdAvengComputeTransformDescriptorSets[index];
				transformWriteDescriptorSet.dstBinding = 0;
				transformWriteDescriptorSet.descriptorCount = 1;
				transformWriteDescriptorSet.pBufferInfo = &transformInfo;

				VkWriteDescriptorSet trsWriteDescriptorSet{};
				trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				trsWriteDescriptorSet.dstSet = renderData.rdAvengComputeTransformDescriptorSets[index];
				trsWriteDescriptorSet.dstBinding = 1;
				trsWriteDescriptorSet.descriptorCount = 1;
				trsWriteDescriptorSet.pBufferInfo = &trsInfo;

				std::vector<VkWriteDescriptorSet> transformWriteDescriptorSets =
				{ transformWriteDescriptorSet, trsWriteDescriptorSet };

				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(transformWriteDescriptorSets.size()),
					transformWriteDescriptorSets.data(), 0, nullptr);
			}

			{
				/* matrix multiplication compute shader, global data */
				VkDescriptorBufferInfo trsInfo{};
				trsInfo.buffer = mShaderTrsMatrixBuffers[index].buffer;
				trsInfo.offset = 0;
				trsInfo.range = VK_WHOLE_SIZE;

				VkDescriptorBufferInfo boneMatrixInfo{};
				boneMatrixInfo.buffer = mShaderBoneMatrixBuffers[index].buffer;
				boneMatrixInfo.offset = 0;
				boneMatrixInfo.range = VK_WHOLE_SIZE;

				VkWriteDescriptorSet trsWriteDescriptorSet{};
				trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				trsWriteDescriptorSet.dstSet = renderData.rdAvengComputeMatrixMultDescriptorSets[index];
				trsWriteDescriptorSet.dstBinding = 0;
				trsWriteDescriptorSet.descriptorCount = 1;
				trsWriteDescriptorSet.pBufferInfo = &trsInfo;

				VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
				boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				boneMatrixWriteDescriptorSet.dstSet = renderData.rdAvengComputeMatrixMultDescriptorSets[index];
				boneMatrixWriteDescriptorSet.dstBinding = 1;
				boneMatrixWriteDescriptorSet.descriptorCount = 1;
				boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

				std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
				{ trsWriteDescriptorSet, boneMatrixWriteDescriptorSet };

				vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
					matrixMultWriteDescriptorSets.data(), 0, nullptr);
			}

		}

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

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			UniformBuffer::cleanup(engineDevice, mPerspectiveViewMatrixUBOBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mShaderTrsMatrixBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mNodeTransformBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mShaderModelRootMatrixBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mShaderBoneMatrixBuffers[i]);
		
			vkFreeDescriptorSets(engineDevice.device(), renderData.avengDescriptorPool, 1, &renderData.rdAvengDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.avengDescriptorPool, 1, &renderData.rdAvengAnimationDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.avengDescriptorPool, 1, &renderData.rdAvengComputeTransformDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.avengDescriptorPool, 1, &renderData.rdAvengComputeMatrixMultDescriptorSets[i]);

		}

		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengAnimationDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengTextureDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengComputeTransformDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengComputeMatrixMultDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengComputeMatrixMultPerModelDescriptorLayout, nullptr);

		vkDestroyDescriptorPool(engineDevice.device(), renderData.avengDescriptorPool, nullptr);

		std::printf("%s: Vulkan renderer destroyed\n", __FUNCTION__);
	}

	bool Renderer::createMatrixUBO() {
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			if (!UniformBuffer::init(engineDevice, mPerspectiveViewMatrixUBOBuffers[i])) {
				Logger::log(1, "%s error: could not create matrix uniform buffers\n", __FUNCTION__);
				return false;
			}
		}

		return true;
	}

	bool Renderer::createSSBOs() {
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			if (!ShaderStorageBuffer::init(engineDevice, mShaderTrsMatrixBuffers[i])) {
				Logger::log(1, "%s error: could not create TRS matrices SSBO\n", __FUNCTION__);
				return false;
			}

			if (!ShaderStorageBuffer::init(engineDevice, mShaderModelRootMatrixBuffers[i])) {
				Logger::log(1, "%s error: could not create nodel root position SSBO\n", __FUNCTION__);
				return false;
			}

			if (!ShaderStorageBuffer::init(engineDevice, mNodeTransformBuffers[i])) {
				Logger::log(1, "%s error: could not create node transform SSBO\n", __FUNCTION__);
				return false;
			}

			if (!ShaderStorageBuffer::init(engineDevice, mShaderBoneMatrixBuffers[i])) {
				Logger::log(1, "%s error: could not create bone matrix SSBO\n", __FUNCTION__);
				return false;
			}
		}

		return true;
	}


} // NS