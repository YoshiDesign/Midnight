#include "Renderer.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/ComputePipeline.h"
#include "CoreVK/LinePipeline.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "CoreVK/AvengUniformBuffer.h"
#include "CoreVK/PipelineLayout.h"
#include "CoreVK/SyncObjects.h"
#include "Core/Modeling/AssimpInstance.h"
#include "Core/aveng_window.h"
#include "Core/aveng_model.h"
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <cstdio>
#include <array>

#define LOG(a) std::cout<<a<<std::endl;
#define DESTROY_UNIFORM_BUFFERS 1	// Unused as far as I can tell

namespace aveng {

	Renderer::Renderer(EngineDevice& engineDevice, AvengWindow& window, VkRenderData& renderData, ModelAndInstanceData& mModelInstanceData)
		: engineDevice{engineDevice}, aveng_window{ window }, renderData{ renderData }, mModelInstanceData{ mModelInstanceData }
	{

		buffer_trash.clear();

		// Define buffer vec's that are managed by the Renderer
		mPerspectiveViewMatrixUBOBuffers = std::vector<VkUniformBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderModelRootMatrixBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mNodeTransformBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderTrsMatrixBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderBoneMatrixBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mLightDataBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdSelectedInstanceBuffers = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT); // This one can be used as needed by the renderer and the editor.

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

		// Initialize our descriptor layouts & sets, map buffers to device memory.
		setupDescriptors();

		// Create pipelines now that descriptor layouts are ready
		if (!createPipelineLayouts()) {
			std::cerr << "error [Renderer 1]!" << std::endl;
			throw std::runtime_error("Failed to create Pipeline Layouts");
		}

		if (!createPipelines()) {
			std::cerr << "error [Renderer 2]!" << std::endl;
			throw std::runtime_error("Failed to create Pipelines");
		}

		if (!createSyncObjects()) {
			std::cerr << "error [Renderer 3]!" << std::endl;
			throw std::runtime_error("Failed to create sync objects");
		}

		// Initialize PointLightSystem now that descriptor layouts are created
		// initializePointLightSystem();

		/* register callbacks */
		mModelInstanceData.miModelCheckCallbackFunction = [this](const std::string& fileName) { return hasModel(fileName); };
		mModelInstanceData.miModelAddCallbackFunction = [this](const std::string& fileName) {/* return addModel(fileName);*/ return queueModelLoad(fileName); };
		mModelInstanceData.miModelDeleteCallbackFunction = [this](const std::string& modelName) { deleteModel(modelName); };

		mModelInstanceData.miInstanceAddCallbackFunction = [this](std::shared_ptr<AvengModel> model) { return addInstance(model); };
		mModelInstanceData.miInstanceAddManyCallbackFunction = [this](std::shared_ptr<AvengModel> model, int numInstances) { addInstances(model, numInstances); };
		mModelInstanceData.miInstanceDeleteCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance) { deleteInstance(instance); };
		mModelInstanceData.miInstanceCloneCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance) { cloneInstance(instance); };
		mModelInstanceData.miInstanceCloneManyCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance, int numClones) { cloneInstances(instance, numClones); };

		{
			/* Null model instance - Utility */
			std::shared_ptr<AvengModel> nullModel = std::make_shared<AvengModel>(engineDevice);
			mModelInstanceData.miModelList.emplace_back(nullModel);
			std::cout << "Null Filename: " << nullModel->getModelFileName() << std::endl;
			std::shared_ptr<AssimpInstance> nullInstance = std::make_shared<AssimpInstance>(nullModel);
			mModelInstanceData.miAssimpInstancesPerModel[nullModel->getModelFileName()].emplace_back(nullInstance);
			mModelInstanceData.miAssimpInstances.emplace_back(nullInstance);
			assignInstanceIndices();
		}

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

		mFrameTimer.start();
		std::printf("Vulkan renderer initialized!\n");

	}

	Renderer::~Renderer()
	{
		std::cout << "Destroying Renderer..." << std::endl;
		freeCommandBuffers();
		cleanup();
		// vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
	}

	bool Renderer::hasModel(const std::string& modelFileName) {
		auto modelIter = std::find_if(mModelInstanceData.miModelList.begin(), mModelInstanceData.miModelList.end(),
			[modelFileName](const auto& model) {
			return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
		});
		return modelIter != mModelInstanceData.miModelList.end();
	}

	std::shared_ptr<AvengModel> Renderer::getModel(const std::string& modelFileName) {
		auto modelIter = std::find_if(mModelInstanceData.miModelList.begin(), mModelInstanceData.miModelList.end(),
			[modelFileName](const auto& model) {
			return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
		});
		if (modelIter != mModelInstanceData.miModelList.end()) {
			return *modelIter;
		}
		return nullptr;
	}

	bool Renderer::addModel(const std::string& modelFileName) {
		
		if (isFrameStarted) {
			std::printf("ERROR: addModel called mid-frame!\n");
			throw std::runtime_error("Internal error: model loading during frame");
		}

		if (hasModel(modelFileName)) {
			std::printf("%s warning: model '%s' already existed, skipping\n", __FUNCTION__, modelFileName.c_str());
			return false;
		}

		std::shared_ptr<AvengModel> model = std::make_shared<AvengModel>(engineDevice);
		if (!model->loadModelV2(renderData, modelFileName)) {
			std::printf("%s error: could not load model file '%s'\n", __FUNCTION__, modelFileName.c_str());
			return false;
		}

		mModelInstanceData.miModelList.emplace_back(model);
		addInstance(model);

		// Maybe
		//if (mModelInstanceData.miAssimpInstances.size() == 2) {
		//	std::shared_ptr<AssimpInstance> firstInstance = mModelInstanceData.miAssimpInstances.at(1);
		//	centerInstance(firstInstance);
		//}

		return true;
	}

	void Renderer::deleteModel(const std::string& modelFileName) {
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

	bool Renderer::queueModelLoad(const std::string& filepath) {
		mModelInstanceData.mPendingModelLoads.push_back(filepath);
		std::printf("Queued model load (will load after current frame)\n");
		return true;
	}

	void Renderer::processPendingModelLoads() {

		if (mModelInstanceData.mPendingModelLoads.empty()) {
			return;
		}

		for (const auto& filepath : mModelInstanceData.mPendingModelLoads) {
			if (hasModel(filepath))
			{
				return;
			}

			std::printf("Processing queued model load: %s\n", filepath.c_str());
			// if (!addModel(pending.filepath)) {
			if (!addModel(filepath)) {
				std::printf("Failed to load queued model: %s\n", filepath.c_str());
			}
			else {
				/* select new model and new instance */
				mModelInstanceData.miSelectedEditorModel = mModelInstanceData.miModelList.size() - 1;
				mModelInstanceData.miSelectedEditorInstance = mModelInstanceData.miAssimpInstances.size() - 1;
			}
		}

		mModelInstanceData.mPendingModelLoads.clear();
	}

	std::shared_ptr<AssimpInstance> Renderer::addInstance(std::shared_ptr<AvengModel> model) {
		std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model);
		mModelInstanceData.miAssimpInstances.emplace_back(newInstance);
		mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);

		assignInstanceIndices();
		updateTriangleCount();

		return newInstance;
	}

	void Renderer::addInstances(std::shared_ptr<AvengModel> model, int numInstances) {
		if (model->hasAnimations()) {
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
		}
		else {

			for (int i = 0; i < numInstances; ++i) {
				int xPos = std::rand() % 50 - 25;
				int zPos = std::rand() % 50 - 25;
				int rotation = std::rand() % 360 - 180;

				std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model, glm::vec3(xPos, 0.0f, zPos), glm::vec3(0.0f, rotation, 0.0f));

				mModelInstanceData.miAssimpInstances.emplace_back(newInstance);
				mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
			}
		
		}
		
		assignInstanceIndices();
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

		assignInstanceIndices();
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

		assignInstanceIndices();
		updateTriangleCount();
	}

	/* keep scaling and axis flipping */
	void Renderer::cloneInstances(std::shared_ptr<AssimpInstance> instance, int numClones) {
		std::shared_ptr<AvengModel> model = instance->getModel();
		size_t animClipNum = model->getAnimClips().size();
		for (int i = 0; i < numClones; ++i) {
			int xPos = std::rand() % 50 - 25;
			int zPos = std::rand() % 50 - 25;
			int rotation = std::rand() % 360 - 180;

			int clipNr = std::rand() % animClipNum;
			float animSpeed = (std::rand() % 50 + 75) / 100.0f;

			std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model);
			InstanceSettings instSettings = instance->getInstanceSettings();
			instSettings.isWorldPosition = glm::vec3(xPos, 0.0f, zPos);
			instSettings.isWorldRotation = glm::vec3(0.0f, rotation, 0.0f);
			if (animClipNum > 0) {
				instSettings.isAnimClipNr = clipNr;
				instSettings.isAnimSpeedFactor = animSpeed;
			}

			newInstance->setInstanceSettings(instSettings);

			mModelInstanceData.miAssimpInstances.emplace_back(newInstance);
			mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
		}

		assignInstanceIndices();
		updateTriangleCount();
	}

	void Renderer::centerInstance(std::shared_ptr<AssimpInstance> instance) {
		InstanceSettings instSettings = instance->getInstanceSettings();

		renderData.rdCameraWorldPosition = instSettings.isWorldPosition + glm::vec3(5.0f);
		/* hard-code values for now, reversing from lookAt() matrix is too much work */
		// TODO - Use our camera's parameters, not these just yet.
		renderData.rdViewAzimuth = 310.0f;
		renderData.rdViewElevation = -15.0f;
	}

	void Renderer::assignInstanceIndices() {
		std::cout << "ASSINGING INDICES:" << std::endl;
		for (size_t i = 0; i < mModelInstanceData.miAssimpInstances.size(); ++i) {
			std::cout 
				<< "modInstanceData.miAssimpInstances[" 
				<< i <<  "] " 
				<< mModelInstanceData.miAssimpInstances.at(i)->getModel()->getModelFileName() 
				<< std::endl;

			InstanceSettings instSettings = mModelInstanceData.miAssimpInstances.at(i)->getInstanceSettings();
			instSettings.isInstanceIndexPosition = i;
			mModelInstanceData.miAssimpInstances.at(i)->setInstanceSettings(instSettings);
		}
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

		std::cout << "Recreating SwapChain!!!" << std::endl;

		recreatingSwapchain = true;

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
	
		aveng_swapchain = nullptr; // This implies that the old swapchain is always VK_NULL_HANDLE - The `else` condition never executes here.

		if (aveng_swapchain == nullptr) {
			std::cout << "Creating Swapchain!!!" << std::endl;
			// Create the new swapchain object
			aveng_swapchain = std::make_unique<SwapChain>(renderData, engineDevice, extent);
		}
		else {
			// 
			std::shared_ptr<SwapChain> oldSwapChain = std::move(aveng_swapchain);
			aveng_swapchain = std::make_unique<SwapChain>(renderData, engineDevice, extent, oldSwapChain);

			if (!oldSwapChain->compareSwapFormats(*aveng_swapchain.get()))
			{
				throw std::runtime_error("Swap chain image format or depth format has changed.");
			}

		}

		recreatingSwapchain = false;

	}

	void Renderer::createCommandBuffers() {

		// Resize our vector of command buffers to match the max number of images the swapchain will allow in 
		renderData.rdCommandBuffersGraphics.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdCommandBuffersCompute.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		// Allocate command buffers from the Graphics Command Pool
		engineDevice.initCommandBuffers(renderData.rdCommandBuffersGraphics);
		// Allocate command buffers from the Compute Command Pool - WARN - The 2nd arg was missing!
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
	bool Renderer::beginFrame()
	{
		assert(!isFrameStarted && "Frame was not ended before a new one began.");
		isFrameStarted = true;

		// Wait for both fences to be signaled before getting the new framebuffer image
		std::vector<VkFence> waitFences = { renderData.rdComputeFence.at(currentFrameIndex), renderData.rdRenderFence.at(currentFrameIndex) };
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
		
		// Acquire an image from the swap chain
		result = vkAcquireNextImageKHR(
			engineDevice.device(),
			aveng_swapchain->getSwapchain(),
			UINT64_MAX, //std::numeric_limits<uint64_t>::max(), // Which is more portable?
			renderData.rdPresentSemaphore.at(currentFrameIndex),  // Can be an unsignaled semaphore, fence, or both
			VK_NULL_HANDLE,
			&currentImageIndex
		);

		// This error will occur after window resize
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			std::cout << "VK_ERROR_OUT_OF_DATE_KHR - Recreating Swapchain" << std::endl;
			recreateSwapChain();
			return false;
		}
		// This could potentially occur during window resize events
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			throw std::runtime_error("Failed to acquire swap chain image.");
		}

		// This is the first point during a new frame where the command buffer is first captured.
		assert(renderData.rdCommandBuffersGraphics.at(currentFrameIndex) == getCurrentCommandBufferGraphics()
			&& "Incorrect Command Buffer in use");
		return true;
	}
	 
	void Renderer::endFrame()
	{
		assert(isFrameStarted && "Can't call endFrame while frame is not in progress.");
		// Trivial sync flag
		isFrameStarted = false;
		// Advance to the next image
		currentFrameIndex = (currentFrameIndex + 1) % SwapChain::MAX_FRAMES_IN_FLIGHT;
	}

	void Renderer::beginSwapChainRenderPass(VkCommandBuffer commandBuffer, VkFramebuffer frameBuffer, VkRenderPass renderpass, bool selection)
	{

		assert(isFrameStarted && "Can't call beginSwapChain if frame is not in progress.");
		// This method is used to begin multiple renderpasses, so this check is irrelevant sometimes
		assert(renderData.rdCommandBuffersGraphics.at(currentFrameIndex) == getCurrentCommandBufferGraphics() &&
			"Can't begin render pass on command buffer from a different frame");

		// Clear Color for now
		glm::vec3 rgb = glm::vec3(0.001f, 0.008f, 0.06f); // Cool, dark midnight blue

		// Render pass info
		VkRenderPassBeginInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderpass;
		renderPassInfo.framebuffer = frameBuffer;

		// The area where shader loading and storing takes place.
		renderPassInfo.renderArea.offset = { 0,0 };
		renderPassInfo.renderArea.extent = aveng_swapchain->getSwapChainExtent();

		std::vector<VkClearValue> colorClearValues;
		VkClearValue colorClearValue;
		colorClearValue.color = { { rgb.r, rgb.g, rgb.b, 1.0f } };
		colorClearValues.emplace_back(colorClearValue);
		if (selection) {
			VkClearValue selectionClearValue;
			selectionClearValue.color = { { -1.0 } }; // C++ aggregate initialization rules
			colorClearValues.emplace_back(selectionClearValue);
		}

		VkClearValue depthValue;
		depthValue.depthStencil.depth = 1.0f;

		std::vector<VkClearValue> clearValues;
		clearValues.insert(clearValues.end(), colorClearValues.begin(), colorClearValues.end());
		clearValues.emplace_back(depthValue);

		renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassInfo.pClearValues = clearValues.data();

		// VK_SUBPASS_CONTENTS_INLINE signals that subsequent renderpass commands come directly from the primary command buffer.
		// No secondary buffers are currently being utilized.
		// For this reason we cannot Mix both Inline command buffers AND secondary command buffers in this render pass's execution.
		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Configure the viewport and scissor
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(aveng_swapchain->getSwapChainExtent().width);
		viewport.height = static_cast<float>(aveng_swapchain->getSwapChainExtent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{ {0, 0}, aveng_swapchain->getSwapChainExtent() };

		// 
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	}

	void Renderer::endSwapChainRenderPass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted && "Can't call endSwapChain if frame is not in progress.");
		assert(commandBuffer == getCurrentCommandBufferGraphics() &&
			"Can't end render pass on command buffer from a different frame");
		vkCmdEndRenderPass(commandBuffer); 
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
		
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++){
			updateDescriptorSets(i);
			updateComputeDescriptorSets(i);
			// TODO
			// updateLightingDescriptorSets(i);
		}
	
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

			VkDescriptorSetLayoutBinding assimpSsboBind2{}; // Selected Instance Data
			assimpSsboBind2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSsboBind2.binding = 2;
			assimpSsboBind2.descriptorCount = 1;
			assimpSsboBind2.pImmutableSamplers = nullptr;
			assimpSsboBind2.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpBindings = { assimpUboBind, assimpSsboBind, assimpSsboBind2 };

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

			VkDescriptorSetLayoutBinding assimpSkinningSsboBind3{};
			assimpSkinningSsboBind3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			assimpSkinningSsboBind3.binding = 3;
			assimpSkinningSsboBind3.descriptorCount = 1;
			assimpSkinningSsboBind3.pImmutableSamplers = nullptr;
			assimpSkinningSsboBind3.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

			std::vector<VkDescriptorSetLayoutBinding> assimpSkinningBindings = { assimpUboBind, assimpSkinningSsboBind, assimpSkinningSsboBind2, assimpSkinningSsboBind3 };

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

	//void Renderer::initializePointLightSystem()
	//{
		//if (!renderData.rdAvengBasicLightingDescriptorLayout) {
		//	throw std::runtime_error("Descriptor set layouts must be created before initializing PointLightSystem (call setupDescriptors first)");
		//}

		//std::cout << "Initializing PointLightSystem" << std::endl;

		//// Initialize point light system using existing descriptor set layouts
		//pointLightSystem.initialize(getSwapChainRenderPass());

		//std::cout << "PointLightSystem initialized" << std::endl;
	//}

	//void Renderer::renderLights()
	//{
		//if (u_LightsData.numLights <= 0) {
		//	return; // Nothing to render
		//}
		//assert(renderData.rdCommandBuffersGraphics.at(currentFrameIndex) == getCurrentCommandBufferGraphics()
		//	&& "Point Light system is using the wrong command buffer");

		//pointLightSystem.getPipeline()->bind(renderData.rdCommandBuffersGraphics.at(currentFrameIndex));

		//// vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdAvengPipeline);

		//// Bind both descriptor sets
		//VkDescriptorSet descriptorSets[2] = { renderData.rdAvengDescriptorSets.at(currentFrameIndex), renderData.basicLightingDescriptorSets.at(currentFrameIndex) };
		//vkCmdBindDescriptorSets(
		//	renderData.rdCommandBuffersGraphics.at(currentFrameIndex),
		//	VK_PIPELINE_BIND_POINT_GRAPHICS,
		//	pointLightSystem.getPipelineLayout(),
		//	0,
		//	2, // binding 2 descriptor sets
		//	descriptorSets,
		//	0,
		//	nullptr);

		//// Use instanced rendering: 6 vertices per light, numLights instances
		//vkCmdDraw(renderData.rdCommandBuffersGraphics.at(currentFrameIndex), 6, u_LightsData.numLights, 0, 0);
	//}

	void Renderer::addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius)
	{
		if (u_LightsData.numLights >= LightsUbo::MAX_LIGHTS) {
			std::cout << "Warning: Maximum number of lights (" << LightsUbo::MAX_LIGHTS << ") reached. Cannot add more lights." << std::endl;
			return;
		}

		u_LightsData.lightPositions[u_LightsData.numLights] = glm::vec4(position, radius);
		u_LightsData.lightColors[u_LightsData.numLights] = glm::vec4(color, intensity);
		u_LightsData.numLights++;
	
	}

	void Renderer::clearLights()
	{

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

		// Note: The non-animated shaders are utilized by the Animation pipeline
		std::string vertexShaderFile = "shaders/assimp.vert.spv";
		std::string fragmentShaderFile = "shaders/assimp.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdAvengPipelineLayout,
			renderData.rdAvengPipeline, renderPass, 1, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp shader pipeline\n", __FUNCTION__);
			return false;
		}

		// Animation Pipeline
		vertexShaderFile = "shaders/assimp_skinning.vert.spv";
		fragmentShaderFile = "shaders/assimp_skinning.frag.spv";
		if (!SkinningPipeline::init(engineDevice,  renderData.rdAvengAnimationPipelineLayout,
			renderData.rdAvengAnimationPipeline, renderPass, 1, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp Skinning shader pipeline\n", __FUNCTION__);
			return false;
		}

		//vertexShaderFile = "shader/aveng_selection.vert.spv";
		//fragmentShaderFile = "shader/aveng_selection.frag.spv";
		//if (!SkinningPipeline::init(engineDevice, renderPass, renderData.rdAvengSelectionPipelineLayout,
		//	renderData.rdAvengSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
		//	Logger::log(1, "%s error: could not init Aveng Selection shader pipeline\n", __FUNCTION__);
		//	return false;
		//}

		//vertexShaderFile = "shader/aveng_skinning_selection.vert.spv";
		//fragmentShaderFile = "shader/aveng_skinning_selection.frag.spv";
		//if (!SkinningPipeline::init(engineDevice, renderPass, renderData.rdAvengSkinningSelectionPipelineLayout,
		//	renderData.rdAvengSkinningSelectionPipeline, vertexShaderFile, fragmentShaderFile)) {
		//	Logger::log(1, "%s error: could not init Assimp Skinning Selection shader pipeline\n", __FUNCTION__);
		//	return false;
		//}

		// Compute Pipeline - Calculates bone transformations throughout animation (writes the updated TRS matrix)
		std::string computeShaderFile = "shaders/assimp_instance_transform.comp.spv";
		if (!ComputePipeline::init(engineDevice, renderData.rdAvengComputeTransformPipelineLayout,
			renderData.rdAvengComputeTransformPipeline, computeShaderFile)) {
			std::printf("%s error: could not init Assimp Transform compute shader pipeline\n", __FUNCTION__);
			return false;
		}

		// Compute Pipeline - multiplies the TRS matrix by each bone to compute their next position
		computeShaderFile = "shaders/assimp_instance_matrix_mult.comp.spv";
		if (!ComputePipeline::init(engineDevice, renderData.rdAvengComputeMatrixMultPipelineLayout,
			renderData.rdAvengComputeMatrixMultPipeline, computeShaderFile)) {
			std::printf("%s error: could not init Assimp Matrix Mult compute shader pipeline\n", __FUNCTION__);
			return false;
		}

		//vertexShaderFile = "shader/line.vert.spv";
		//fragmentShaderFile = "shader/line.frag.spv";
		//if (!LinePipeline::init(engineDevice, renderPass, renderData.rdLinePipelineLayout, renderData.rdLinePipeline,
		//	vertexShaderFile, fragmentShaderFile)) {
		//	Logger::log(1, "%s error: could not init Assimp line drawing shader pipeline\n", __FUNCTION__);
		//	return false;
		//}

		return true;
	}

	size_t Renderer::calculateDynamicUBOStride() const
	{
		size_t objectSize = sizeof(ObjectUniformData);
		size_t minAlignment = engineDevice.properties.limits.minUniformBufferOffsetAlignment;
		return ((objectSize + minAlignment - 1) / minAlignment) * minAlignment;
	}

	void Renderer::runComputeShaders(std::shared_ptr<AvengModel> model, int numInstances, uint32_t modelOffset) {
		uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());
		// VkCommandBuffer computeCommandBuffer = getCurrentCommandBufferCompute();

		/* node transformation */
		vkCmdBindPipeline(renderData.rdCommandBuffersCompute.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeTransformPipeline);

		// std::cout << "Check [0]" << std::endl;
		vkCmdBindDescriptorSets(renderData.rdCommandBuffersCompute.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeTransformPipelineLayout, 0, 1, &renderData.rdAvengComputeTransformDescriptorSets.at(currentFrameIndex), 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(renderData.rdCommandBuffersCompute.at(currentFrameIndex), renderData.rdAvengComputeTransformPipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &mComputeModelData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		// std::printf("Dispatching compute: %d x %d x 1\n", numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)));

		vkCmdDispatch(renderData.rdCommandBuffersCompute.at(currentFrameIndex), numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);
		/*std::printf("Compute dispatch called: bones=%d, instanceGroups=%d\n",
			numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)));*/

		/* memory barrier between the compute shaders
		 * wait for TRS buffer to be written  */
		VkBufferMemoryBarrier trsBufferBarrier{};
		trsBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		trsBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		trsBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		trsBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		trsBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		trsBufferBarrier.buffer = mShaderTrsMatrixBuffers.at(currentFrameIndex).buffer;
		trsBufferBarrier.offset = 0;
		trsBufferBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(renderData.rdCommandBuffersCompute.at(currentFrameIndex), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
			&trsBufferBarrier, 0, nullptr);

		/* matrix multiplication */
		vkCmdBindPipeline(renderData.rdCommandBuffersCompute.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeMatrixMultPipeline);

		VkDescriptorSet& modelDescriptorSet = model->getMatrixMultDescriptorSet(currentFrameIndex);
		std::vector<VkDescriptorSet> computeSets = { renderData.rdAvengComputeMatrixMultDescriptorSets.at(currentFrameIndex), modelDescriptorSet }; 
		vkCmdBindDescriptorSets(renderData.rdCommandBuffersCompute.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(computeSets.size()), computeSets.data(), 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(renderData.rdCommandBuffersCompute.at(currentFrameIndex), renderData.rdAvengComputeMatrixMultPipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &mComputeModelData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		vkCmdDispatch (renderData.rdCommandBuffersCompute.at(currentFrameIndex), numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

		/* memory barrier after compute shader
		 * wait for bone matrix buffer to be written  */
		VkBufferMemoryBarrier boneMatrixBufferBarrier{};
		boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		boneMatrixBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		boneMatrixBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		boneMatrixBufferBarrier.buffer = mShaderBoneMatrixBuffers.at(currentFrameIndex).buffer;
		boneMatrixBufferBarrier.offset = 0;
		boneMatrixBufferBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(renderData.rdCommandBuffersCompute.at(currentFrameIndex), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
			&boneMatrixBufferBarrier, 0, nullptr);
	}

	/**
	* By the time this method is called:
	* - Fences have all been waited on and signaled.
	* - The latest swapchain image has been received
	* - isFrameStarted = true
	* - Primary Graphics Command buffer is reset and in a recording state.
	* - Primary graphics renderpass has begun
	*/
	int Renderer::draw(float deltaTime) {

		/* no update on zero diff. This caused an issue */
		if (deltaTime == 0.0f && !firstFrame) {
			isFrameStarted = false;
			return 0;
		}

		renderData.rdFrameTime = mFrameTimer.stop();
		mFrameTimer.start();

		/* reset timers and other values */
		renderData.rdMatricesSize = 0;
		renderData.rdUploadToUBOTime = 0.0f;
		renderData.rdUploadToVBOTime = 0.0f;
		renderData.rdMatrixGenerateTime = 0.0f;
		renderData.rdUIGenerateTime = 0.0f;

		// beginFrame(); // This is now managed by Frame

		if (!isFrameStarted) {
			std::printf("beginFrame failed/skipped (swapchain recreation), skipping frame\n");
			return 0;  // Skip this frame gracefully
		}

		/* calculate the size of the node matrix buffer over all animated instances */
		boneMatrixBufferSize = 0;
		for (const auto& model : mModelInstanceData.miModelList) {
			size_t numberOfInstances = mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()].size();
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

		for (const auto& model : mModelInstanceData.miModelList) { //

			size_t numberOfInstances = mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()].size(); // second is the vector of <shared_ptr> AssimpInstance

			if (numberOfInstances > 0 && model->getTriangleCount() > 0) {
				/* animated models */
				if (model->hasAnimations() && !model->getBoneList().empty()) {

					// Collect the number of bones
					size_t numberOfBones = model->getBoneList().size();
					animatedModelLoaded = true;

					mMatrixGenerateTimer.start();
					// std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()];

					int instIndex = 0;
					
					// For each instance
					//for (unsigned int i = 0; i < numberOfInstances; ++i) {
					for (const auto& instance : mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()]) {

						//auto& instance = instances[i];

						instance->updateAnimation(deltaTime);
						std::vector<NodeTransformData> instanceNodeTransform = instance->getNodeTransformData();

						// Copy the NodeTransform Data to the vector of NodeTransform datas. I didn't know you can use arithmetic with iterator access patterns. Nice
						// STORAGE BUFFER DATA - Packed with every instance's data
						std::copy(instanceNodeTransform.begin(), instanceNodeTransform.end(), mNodeTransFormData.begin() + animatedInstancesToStore + instIndex * numberOfBones);

						// STORAGE BUFFER DATA - Packed with every instance's data
						mWorldPosMatrices.at(instanceToStore + instIndex) = instance->getWorldTransformMatrix(); // model Root Matrix SSBO data 
						instIndex++;
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
					int instIndex = 0;
					//std::vector<std::shared_ptr<AssimpInstance>> instances = mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()];
					//for (unsigned int i = 0; i < numberOfInstances; ++i) {
					for (const auto& instance : mModelInstanceData.miAssimpInstancesPerModel[model->getModelFileName()]) {
						//auto& instance = instances[i];
						mWorldPosMatrices.at(instanceToStore + instIndex) = instance->getWorldTransformMatrix(); // model Root Matrix SSBO data 
						instIndex++;
					}

					renderData.rdMatrixGenerateTime += mMatrixGenerateTimer.stop();
					renderData.rdMatricesSize += numberOfInstances * sizeof(glm::mat4);

					instanceToStore += numberOfInstances;
				}
			}
		}

		mUploadToUBOTimer.start();

		/*
		* TIL: mNodeTransFormData is updated via instance->updateAnimation()
		*	   That is also when & where we update mWorldPosMatrices.
		*	   TRS and BoneMat buffers are updated by the compute shaders so you won't see uploadSsboData here on their behalf
		*/

		/*
		* TODO: Whenever I re-architect this resizing logic: I could be re-initializing both buffers so that the subsequent frame doesn't need
		* to also go through this, if it even works. This means I'd have to somehow get both buffers destroyed in 1 frame, whenever that becomes safe to do.
		*/

		bool bufferResized = false;
		bufferResized = ShaderStorageBuffer::uploadSsboData(engineDevice, mNodeTransformBuffers.at(currentFrameIndex), mNodeTransFormData);
		// Upload every instance's current transform data (translation, scale, rotation)
		// If it resized, no data was uploaded
		if (bufferResized) {

			std::cout << "NodeTransform Buffer Resized" << std::endl;

			size_t newBufferSize = std::max(mNodeTransFormData.size() * (sizeof NodeTransformData), mNodeTransformBuffers.at(currentFrameIndex).bufferSize * 2);

			// Queue the old buffers for destruction within the next frame
			buffer_trash.push_back(
				PendingBufferDestroy{
					mNodeTransformBuffers.at(currentFrameIndex).buffer,
					mNodeTransformBuffers.at(currentFrameIndex).bufferAlloc,
				}
			);

			// Reinitialize - New Allocation
			ShaderStorageBuffer::init(engineDevice, mNodeTransformBuffers.at(currentFrameIndex), newBufferSize);

			// Retry the upload - true == it resized again after we just tried to reallocate it. That would be bad
			if (ShaderStorageBuffer::uploadSsboData(engineDevice, mNodeTransformBuffers.at(currentFrameIndex), mNodeTransFormData))
			{
				std::printf("[1] Failed to accommodate resized buffer.\n");
				throw std::runtime_error("[1] Failed to accommodate resized buffer.");
			}

		}

		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		/* resize SSBO if needed - Both of these are written by the compute shaders so we just make sure they're the right size (hence only using checkForResize) */
		
		bool trsResized = ShaderStorageBuffer::checkForResize(engineDevice, mShaderTrsMatrixBuffers.at(currentFrameIndex), boneMatrixBufferSize * sizeof(glm::mat4));
		bool boneResized = ShaderStorageBuffer::checkForResize(engineDevice, mShaderBoneMatrixBuffers.at(currentFrameIndex), boneMatrixBufferSize * sizeof(glm::mat4));

		bufferResized |= trsResized;
		bufferResized |= boneResized;

		if (trsResized || boneResized) {

			std::cout << "TRSMat | BoneMat Buffers Resized" << std::endl;
			
			// TRS and BoneMat buffers are very similar, as you can tell from this size derivation - TODO - Just give each their own...
			size_t newBufferSize = std::max(boneMatrixBufferSize * sizeof(glm::mat4), mShaderTrsMatrixBuffers.at(currentFrameIndex).bufferSize * 2);

			// Queue the old buffers for destruction within the next frame
			buffer_trash.push_back(
				PendingBufferDestroy{
					mShaderTrsMatrixBuffers.at(currentFrameIndex).buffer,
					mShaderTrsMatrixBuffers.at(currentFrameIndex).bufferAlloc,
				}
			);

			buffer_trash.push_back(
				PendingBufferDestroy{
					mShaderBoneMatrixBuffers.at(currentFrameIndex).buffer,
					mShaderBoneMatrixBuffers.at(currentFrameIndex).bufferAlloc,
				}
			);

			// Reinitialize boneMat buffers
			ShaderStorageBuffer::init(
				engineDevice,
				mShaderBoneMatrixBuffers.at(currentFrameIndex),
				newBufferSize // New buffer size - these buffers are very similar
			);

			// Reinitialize TrsMat buffers
			ShaderStorageBuffer::init(
				engineDevice,
				mShaderTrsMatrixBuffers.at(currentFrameIndex),
				newBufferSize // New buffer size - these buffers are very similar
			);

			/* Note that we're not performing another SSBO Upload. 
			That's because these two buffers are written by the compute shader */

		}

		if (bufferResized)
		{
			std::cout << "StorageBuffer Resized - Updating Descriptor Sets" << std::endl;
			updateDescriptorSets(currentFrameIndex);
			updateComputeDescriptorSets(currentFrameIndex);
		}

		/* record compute commands */
		result = vkResetFences(engineDevice.device(), 1, &renderData.rdComputeFence.at(currentFrameIndex));
		if (result != VK_SUCCESS) {
			std::printf("%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
			return WTF_BOOM;
		}

		if (animatedModelLoaded) {

			/*
			* Command Buffer recording for the Compute queue
			*/

			if (!engineDevice.resetCommandBuffer(renderData.rdCommandBuffersCompute.at(currentFrameIndex), 0)) {
				std::printf("%s error: failed to reset compute command buffer\n", __FUNCTION__);
				return WTF_BOOM;
			}

			if (!engineDevice.beginSingleShotCommand(renderData.rdCommandBuffersCompute.at(currentFrameIndex))) {
				std::printf("%s error: failed to begin compute command buffer\n", __FUNCTION__);
				return WTF_BOOM;
			}

			uint32_t computeShaderModelOffset = 0;
			for (const auto& modelType : mModelInstanceData.miAssimpInstancesPerModel) {
				size_t numberOfInstances = modelType.second.size();
				std::shared_ptr<AvengModel> model = modelType.second.at(0)->getModel();
				if (numberOfInstances > 0 && model->getTriangleCount() > 0) {

					/* compute shader for animated models only */
					if (model->hasAnimations() && !model->getBoneList().empty()) {
						size_t numberOfBones = model->getBoneList().size();

						//std::printf("[runComputeShaders] Buffer sizes - NodeTransform: %zu, TRSMatrix: %zu, BoneMatrix: %zu\n",
						//	mNodeTransformBuffers.at(currentFrameIndex).bufferSize,
						//	mShaderTrsMatrixBuffers.at(currentFrameIndex).bufferSize,
						//	mShaderBoneMatrixBuffers.at(currentFrameIndex).bufferSize);

						runComputeShaders(model, numberOfInstances, computeShaderModelOffset);

						computeShaderModelOffset += numberOfInstances * numberOfBones;
					}
				}
			}

			// End command recording for Compute Queue
			if (!engineDevice.endCommandBuffer(renderData.rdCommandBuffersCompute.at(currentFrameIndex))) {
				std::printf("%s error: failed to end compute command buffer\n", __FUNCTION__);
				return WTF_BOOM;
			}

			/* submit compute commands */
			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			VkSubmitInfo computeSubmitInfo{};
			computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			computeSubmitInfo.commandBufferCount = 1;
			computeSubmitInfo.pCommandBuffers = &renderData.rdCommandBuffersCompute.at(currentFrameIndex);
			computeSubmitInfo.signalSemaphoreCount = 1;
			computeSubmitInfo.pSignalSemaphores = &renderData.rdComputeSemaphore.at(currentFrameIndex);
			computeSubmitInfo.waitSemaphoreCount = 1;
			computeSubmitInfo.pWaitSemaphores = &renderData.rdGraphicSemaphore.at(currentFrameIndex);
			computeSubmitInfo.pWaitDstStageMask = &waitStage;

			result = vkQueueSubmit(engineDevice.computeQueue(), 1, &computeSubmitInfo, renderData.rdComputeFence.at(currentFrameIndex));
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
				return WTF_BOOM;
			};

		}
		else {
			/* do an empty submit if we don't have animated models to satisfy fence and semaphor */
			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			VkSubmitInfo computeSubmitInfo{};
			computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			computeSubmitInfo.signalSemaphoreCount = 1;
			computeSubmitInfo.pSignalSemaphores = &renderData.rdComputeSemaphore.at(currentFrameIndex);
			computeSubmitInfo.waitSemaphoreCount = 1;
			computeSubmitInfo.pWaitSemaphores = &renderData.rdGraphicSemaphore.at(currentFrameIndex);
			computeSubmitInfo.pWaitDstStageMask = &waitStage;

			// Compute submission and fence signaling
			result = vkQueueSubmit(engineDevice.computeQueue(), 1, &computeSubmitInfo, renderData.rdComputeFence.at(currentFrameIndex));
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
				return WTF_BOOM;
			};
		}

		/* we need to update descriptors after the upload if buffer size changed */
		mUploadToUBOTimer.start();

		// TODO - Skip this upload if the camera's data hasn't changed (the view isn't moving).
		UniformBuffer::uploadData(engineDevice, mPerspectiveViewMatrixUBOBuffers.at(currentFrameIndex), mMatrices);

		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		// TODO - Figure out why this occurs AFTER compute
		if (ShaderStorageBuffer::uploadSsboData(engineDevice, mShaderModelRootMatrixBuffers.at(currentFrameIndex), mWorldPosMatrices)) {
			size_t newBufferSize = std::max(mWorldPosMatrices.size() * sizeof glm::mat4, mShaderModelRootMatrixBuffers.at(currentFrameIndex).bufferSize * 2);
			
			/*
				TODO - GC The old buffer
			*/

			// Reinitialize TrsMat buffers
			ShaderStorageBuffer::init(
				engineDevice,
				mShaderModelRootMatrixBuffers.at(currentFrameIndex),
				newBufferSize // New buffer size
			);

			if (ShaderStorageBuffer::uploadSsboData(engineDevice, mShaderModelRootMatrixBuffers.at(currentFrameIndex), mWorldPosMatrices)) {
				std::printf("[2] Failed to accommodate resized buffer.\n");
				throw std::runtime_error("[2] Failed to accommodate resized buffer.");
			}
		
			std::cout << "Model Root SSBO Resized - Updating Descriptor Sets" << std::endl;
			updateDescriptorSets(currentFrameIndex);
			updateComputeDescriptorSets(currentFrameIndex);
		}

	}

	void Renderer::beginGraphicsCommands(int frameIndex)
	{

		assert(renderData.rdCommandBuffersGraphics[frameIndex] == getCurrentCommandBufferGraphics() &&
			"Wrong CB for Graphics");

		if (!engineDevice.resetCommandBuffer(renderData.rdCommandBuffersGraphics[frameIndex], 0)) {
			std::printf("%s error: failed to reset command buffer\n", __FUNCTION__);
			throw std::runtime_error("Failed to begin Graphics Command Buffer 1");
		}

		if (!engineDevice.beginSingleShotCommand(renderData.rdCommandBuffersGraphics[frameIndex])) {
			std::printf("%s error: failed to begin command buffer\n", __FUNCTION__);
			throw std::runtime_error("Failed to begin Graphics Command Buffer 0");
		}

	}

	void Renderer::endGraphicsCommands(int frameIndex)
	{
		VkResult result = vkEndCommandBuffer(renderData.rdCommandBuffersGraphics[frameIndex]);
		if (result != VK_SUCCESS) {
			std::printf("%s error: could not end render pass (error: %i)\n", __FUNCTION__, result);
			throw std::runtime_error("error: could not end render pass");
		}

	}

	/**
	* Note: This method is just for clients to be able to utilize model draws (e.g. the Editor).
	* For one less stack frame, you could copy/paste this code directly 
	* into the renderer.draw() method. Args are super lightweight though
	*/
	bool Renderer::drawModels(
		VkCommandBuffer commandBuffer,
		VkPipeline basicPipeline,
		VkPipeline animationPipeline,
		VkPipelineLayout basicLayout,
		VkPipelineLayout animationLayout,
		VkDescriptorSet basicDescriptorSet,
		VkDescriptorSet animationDescriptorSet,
		int frameIndex)
	{

		/* draw the models */
		uint32_t worldPosOffset = 0;
		uint32_t skinMatOffset = 0;
		for (const auto& modelType : mModelInstanceData.miAssimpInstancesPerModel) 
		{
			size_t numberOfInstances = modelType.second.size();
			std::shared_ptr<AvengModel> model = modelType.second.at(0)->getModel();

			if (numberOfInstances > 0 && model->getTriangleCount() > 0) 
			{

				/* animated models */
				if (model->hasAnimations() && !model->getBoneList().empty()) 
				{
					uint32_t numberOfBones = static_cast<uint32_t>(model->getBoneList().size());

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, animationPipeline);

					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
						animationLayout, 1, 1, &animationDescriptorSet, 0, nullptr);

					mUploadToUBOTimer.start();

					// The number of bones in the model
					mModelData.pkModelStride = numberOfBones;
					// An index to the first location of this model's instances.
					mModelData.pkWorldPosOffset = worldPosOffset;
					// An index to the first location of this model's bones.
					mModelData.pkSkinMatOffset = skinMatOffset;

					vkCmdPushConstants(commandBuffer, animationLayout,
						VK_SHADER_STAGE_VERTEX_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mModelData);

					renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

					model->drawInstancedV2(renderData, basicLayout, animationLayout, numberOfInstances, frameIndex);
					worldPosOffset += numberOfInstances;
					skinMatOffset += numberOfInstances * numberOfBones;

				} else {
					/* non-animated models */

					vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, basicPipeline);

					vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
						basicLayout, 1, 1, &basicDescriptorSet, 0, nullptr);

					mUploadToUBOTimer.start();
					mModelData.pkModelStride = 0;
					mModelData.pkWorldPosOffset = worldPosOffset;
					mModelData.pkSkinMatOffset = 0;
					vkCmdPushConstants(commandBuffer, basicLayout,
						VK_SHADER_STAGE_VERTEX_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mModelData);
					renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

					// Note: We pass the animation layout here even though its implicitly basic
					model->drawInstancedV2(renderData, basicLayout, animationLayout, numberOfInstances, frameIndex);
					worldPosOffset += numberOfInstances;

				}
			}
		}

		return true;
	}

	void Renderer::updateDescriptorSets(int frameIndex) {
		Logger::log(1, "%s: updating descriptor sets\n", __FUNCTION__);

		/* we must update the descriptor sets whenever the buffer size has changed */
		{
			/* non-animated shader */
			VkDescriptorBufferInfo matrixInfo{};
			matrixInfo.buffer = mPerspectiveViewMatrixUBOBuffers[frameIndex].buffer;
			matrixInfo.offset = 0;
			matrixInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo worldPosInfo{};
			worldPosInfo.buffer = mShaderModelRootMatrixBuffers[frameIndex].buffer;
			worldPosInfo.offset = 0;
			worldPosInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo selectionInfo{};
			selectionInfo.buffer = renderData.rdSelectedInstanceBuffers[frameIndex].buffer;
			selectionInfo.offset = 0;
			selectionInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet matrixWriteDescriptorSet{};
			matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			matrixWriteDescriptorSet.dstSet = renderData.rdAvengDescriptorSets[frameIndex];
			matrixWriteDescriptorSet.dstBinding = 0;
			matrixWriteDescriptorSet.descriptorCount = 1;
			matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

			VkWriteDescriptorSet posWriteDescriptorSet{};
			posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			posWriteDescriptorSet.dstSet = renderData.rdAvengDescriptorSets[frameIndex];
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
			matrixInfo.buffer = mPerspectiveViewMatrixUBOBuffers[frameIndex].buffer;
			matrixInfo.offset = 0;
			matrixInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo boneMatrixInfo{};
			boneMatrixInfo.buffer = mShaderBoneMatrixBuffers[frameIndex].buffer;
			boneMatrixInfo.offset = 0;
			boneMatrixInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo worldPosInfo{};
			worldPosInfo.buffer = mShaderModelRootMatrixBuffers[frameIndex].buffer;
			worldPosInfo.offset = 0;
			worldPosInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo selectionInfo{};
			selectionInfo.buffer = renderData.rdSelectedInstanceBuffers[frameIndex].buffer;
			selectionInfo.offset = 0;
			selectionInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet matrixWriteDescriptorSet{};
			matrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			matrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			matrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationDescriptorSets[frameIndex];
			matrixWriteDescriptorSet.dstBinding = 0;
			matrixWriteDescriptorSet.descriptorCount = 1;
			matrixWriteDescriptorSet.pBufferInfo = &matrixInfo;

			VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
			boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			boneMatrixWriteDescriptorSet.dstSet = renderData.rdAvengAnimationDescriptorSets[frameIndex];
			boneMatrixWriteDescriptorSet.dstBinding = 1;
			boneMatrixWriteDescriptorSet.descriptorCount = 1;
			boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

			VkWriteDescriptorSet posWriteDescriptorSet{};
			posWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			posWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			posWriteDescriptorSet.dstSet = renderData.rdAvengAnimationDescriptorSets[frameIndex];
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

	void Renderer::updateLightingDescriptorSets(int frameIndex) {

		//int i = currentFrameIndex;

		//// TODO
		//auto lightsBufferInfo = mLightDataBuffers[i]->descriptorInfo(sizeof(LightsUbo), 0);
		//AvengDescriptorSetWriter(*renderData.rdAvengBasicLightingDescriptorLayout, *renderData.avengDescriptorPool)
		//	.writeBuffer(0, &lightsBufferInfo)
		//	.build(renderData.basicLightingDescriptorSets[i]);
		
	}

	void Renderer::updateComputeDescriptorSets(int frameIndex) {

		Logger::log(1, "%s: updating compute descriptor sets\n", __FUNCTION__);

		{
			/* transform compute shader */
			VkDescriptorBufferInfo transformInfo{};
			transformInfo.buffer = mNodeTransformBuffers[frameIndex].buffer;
			transformInfo.offset = 0;
			transformInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo trsInfo{};
			trsInfo.buffer = mShaderTrsMatrixBuffers[frameIndex].buffer;
			trsInfo.offset = 0;
			trsInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet transformWriteDescriptorSet{};
			transformWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			transformWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			transformWriteDescriptorSet.dstSet = renderData.rdAvengComputeTransformDescriptorSets[frameIndex];
			transformWriteDescriptorSet.dstBinding = 0;
			transformWriteDescriptorSet.descriptorCount = 1;
			transformWriteDescriptorSet.pBufferInfo = &transformInfo;

			VkWriteDescriptorSet trsWriteDescriptorSet{};
			trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			trsWriteDescriptorSet.dstSet = renderData.rdAvengComputeTransformDescriptorSets[frameIndex];
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
			trsInfo.buffer = mShaderTrsMatrixBuffers[frameIndex].buffer;
			trsInfo.offset = 0;
			trsInfo.range = VK_WHOLE_SIZE;

			VkDescriptorBufferInfo boneMatrixInfo{};
			boneMatrixInfo.buffer = mShaderBoneMatrixBuffers[frameIndex].buffer;
			boneMatrixInfo.offset = 0;
			boneMatrixInfo.range = VK_WHOLE_SIZE;

			VkWriteDescriptorSet trsWriteDescriptorSet{};
			trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			trsWriteDescriptorSet.dstSet = renderData.rdAvengComputeMatrixMultDescriptorSets[frameIndex];
			trsWriteDescriptorSet.dstBinding = 0;
			trsWriteDescriptorSet.descriptorCount = 1;
			trsWriteDescriptorSet.pBufferInfo = &trsInfo;

			VkWriteDescriptorSet boneMatrixWriteDescriptorSet{};
			boneMatrixWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			boneMatrixWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			boneMatrixWriteDescriptorSet.dstSet = renderData.rdAvengComputeMatrixMultDescriptorSets[frameIndex];
			boneMatrixWriteDescriptorSet.dstBinding = 1;
			boneMatrixWriteDescriptorSet.descriptorCount = 1;
			boneMatrixWriteDescriptorSet.pBufferInfo = &boneMatrixInfo;

			std::vector<VkWriteDescriptorSet> matrixMultWriteDescriptorSets =
			{ trsWriteDescriptorSet, boneMatrixWriteDescriptorSet };

			std::cout
				<< "[DS BONEMAT UPDATE] frame=" << currentFrameIndex
				<< " set=1 binding=1"
				<< " buffer=" << mShaderTrsMatrixBuffers.at(currentFrameIndex).buffer
				<< " range=" << boneMatrixInfo.range
				<< " ssboSize=" << mShaderTrsMatrixBuffers.at(currentFrameIndex).bufferSize
				<< std::endl;

			vkUpdateDescriptorSets(engineDevice.device(), static_cast<uint32_t>(matrixMultWriteDescriptorSets.size()),
				matrixMultWriteDescriptorSets.data(), 0, nullptr);
		}



	}

	void Renderer::updateCamera()
	{
		// TODO - Make sure these don't contain garbage values before assigned
		mMatrices.projectionMatrix = renderData.cameraProxy.projection;
		mMatrices.viewMatrix = renderData.cameraProxy.view;
		// Maybe ambient light as well
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

		// Populate the shared view for the editor
		renderData.matrixBuffersView.viewProjUBOs = {
			mPerspectiveViewMatrixUBOBuffers.data(),
			mPerspectiveViewMatrixUBOBuffers.size()
		};
			
		return true;
	}

	bool Renderer::createSSBOs() {
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {\

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

		// Populate the shared view for the editor
		renderData.matrixBuffersView.modelRootSSBOs = {
			mShaderModelRootMatrixBuffers.data(),
			mShaderModelRootMatrixBuffers.size()
		};
		renderData.matrixBuffersView.boneMatSSBOs = {
			mShaderBoneMatrixBuffers.data(),
			mShaderBoneMatrixBuffers.size()
		};

		return true;
	}

	/* Check the destruction queue for impending doom */
	void Renderer::destroyTrash()
	{
		if (buffer_trash.size() > 0) {
			for (auto& pending : buffer_trash) {
				std::cout << "Destroying Buffer from Renderer" << std::endl;
				vmaDestroyBuffer(engineDevice.allocator(), pending.buffer, pending.allocation);
			}
			buffer_trash.clear();
		}
		return;
	}

} // NS