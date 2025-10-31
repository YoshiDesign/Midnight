#include "Renderer.h"
#include <iostream>
#include <stdexcept>
#include <cassert>
#include <array>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include "../Animation/AssimpInstance.h"
#include "../Animation/AnimationRenderingSystem.h"

#define LOG(a) std::cout<<a<<std::endl;
#define DESTROY_UNIFORM_BUFFERS 1	// Unused as far as I can tell

namespace aveng {

	// Push constants - now properly handled
	struct PushConstantData {
		glm::mat4 modelMatrix;
		glm::mat4 normalMatrix;
		int32_t boneMatrixOffset;  // For compatibility with animation system (unused here)
	};

	Renderer::Renderer(AvengWindow& window, GameData& _gameData) : aveng_window{ window }, gameData{_gameData}
	{

		// Raw ptr access to engineDevice - This is a feature
		// renderData.engineDevice = &engineDevice;

		recreateSwapChain();
		createCommandBuffers();

		sceneLoader.load(default_scene_file, engineDevice);

		// TODO - if (!scenes) throw error

		//// Delegate to renderer for all engine-specific setup
		//int numObjects = static_cast<int>(sceneLoader.getObjectCount());
		//if (numObjects == 0) numObjects = 1; // Prevent crash with empty scenes

		// Initialize ImageSystem with scene textures
		const auto& sceneTextures = sceneLoader.getSceneTextures();
		std::cout << "Scene has " << sceneTextures.size() << " textures defined" << std::endl;
		initializeImageSystem(sceneTextures);

		// Initialize our descriptor sets, map buffers to device memory.
		setupDescriptors();

		// Create pipelines now that descriptor layouts are ready
		createPipelines();

		// Initialize PointLightSystem now that descriptor layouts are created
		initializePointLightSystem();

		//animationSystem = std::make_unique<AnimationRenderingSystem>(engineDevice);

		editor.init(window, aveng_swapchain.get());

	}

	Renderer::~Renderer()
	{
		std::cout << "Destroying Renderer..." << std::endl;
		renderBatches.clear();
		freeCommandBuffers();
		vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
	}

	void Renderer::loadScenes(const char* filepath)
	{

	}

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

		// Resize our vector of command buffers to match the max number of images the swapchain will allow in flight
		renderData.rdCommandBuffersGraphics.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdCommandBuffersCompute.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		// Command buffer memory is allocated from a command pool
		allocInfo.commandPool = engineDevice.commandPoolGraphics();
		allocInfo.commandBufferCount = static_cast<uint32_t>(renderData.rdCommandBuffersGraphics.size());

		if (vkAllocateCommandBuffers(engineDevice.device(), &allocInfo, renderData.rdCommandBuffersGraphics.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate command buffers.");
		}

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		// Command buffer memory is allocated from a command pool
		allocInfo.commandPool = renderData.rdHasDedicatedComputeQueue ? engineDevice.commandPoolCompute() : engineDevice.commandPoolGraphics();
		allocInfo.commandBufferCount = static_cast<uint32_t>(renderData.rdCommandBuffersCompute.size());

		if (vkAllocateCommandBuffers(engineDevice.device(), &allocInfo, renderData.rdCommandBuffersCompute.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate command buffers.");
		}

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

		auto result = aveng_swapchain->acquireNextImage(&currentImageIndex);

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

		renderData.rdCommandBufferGraphics = getCurrentCommandBufferGraphics();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (vkBeginCommandBuffer(renderData.rdCommandBufferGraphics, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("Command Buffer failed to begin recording.");
		}

		/**
		 * Next Step, begin the render pass
		 */

		// Clear Color for now
		glm::vec3 clear_color = glm::vec3(0.001f, 0.008f, 0.06f); // Cool, dark midnight blue
		beginSwapChainRenderPass(renderData.rdCommandBufferGraphics, clear_color);

	}

	// 
	void  Renderer::endFrame()
	{
		assert(isFrameStarted && "Can't call endFrame while frame is not in progress.");

		
		renderData.rdCommandBufferGraphics = getCurrentCommandBufferGraphics();
		endSwapChainRenderPass(renderData.rdCommandBufferGraphics);
		if (vkEndCommandBuffer(renderData.rdCommandBufferGraphics) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to record command buffer.");
		}


		// Submit to graphics queue while handling cpu and gpu sync, executing the command buffers
		auto result = aveng_swapchain->submitCommandBuffers(&renderData.rdCommandBufferGraphics, &currentImageIndex);

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


	void  Renderer::beginSwapChainRenderPass(VkCommandBuffer _commandBufferGraphics, glm::vec3 rgb)
	{
	
		assert(isFrameStarted && "Can't call beginSwapChain if frame is not in progress.");
		assert(
			_commandBufferGraphics == getCurrentCommandBufferGraphics() && 
			"Can't begin render pass on command buffer from a different frame");


		/*
			Record Commands
		*/

		// 1. Begin a render pass
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

		// 2. Submit to command buffers to begin the render pass

		// VK_SUBPASS_CONTENTS_INLINE signals that subsequent renderpass commands come directly from the primary command buffer.
		// No secondary buffers are currently being utilized.
		// For this reason we cannot Mix both Inline command buffers AND secondary command buffers in our render pass execution.
		vkCmdBeginRenderPass(_commandBufferGraphics, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

		// Configure the viewport and scissor
		VkViewport viewport{};
		viewport.x = 0.0f;
		viewport.y = 0.0f;
		viewport.width = static_cast<float>(aveng_swapchain->getSwapChainExtent().width);
		viewport.height = static_cast<float>(aveng_swapchain->getSwapChainExtent().height);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		VkRect2D scissor{ {0, 0}, aveng_swapchain->getSwapChainExtent() };
		vkCmdSetViewport(_commandBufferGraphics, 0, 1, &viewport);
		vkCmdSetScissor(_commandBufferGraphics, 0, 1, &scissor);

	}

	void  Renderer::endSwapChainRenderPass(VkCommandBuffer _commandBufferGraphics)
	{
		assert(isFrameStarted && "Can't call endSwapChain if frame is not in progress.");
		assert(
			_commandBufferGraphics == getCurrentCommandBufferGraphics() &&
			"Can't end render pass on command buffer from a different frame");
		vkCmdEndRenderPass(_commandBufferGraphics);
	}

	void Renderer::initializeImageSystem(const std::vector<std::string>& texturePaths)
	{
		std::cout << "Initializing ImageSystem with " << texturePaths.size() << " textures from scene" << std::endl;
		
		if (texturePaths.empty()) {
			// Use empty texture list - ImageSystem will handle this gracefully
			imageSystem = std::make_unique<ImageSystem>(engineDevice, std::vector<std::string>());
			currentTextureCount = 1; // Minimum of 1 for empty scenes
		} else {
			imageSystem = std::make_unique<ImageSystem>(engineDevice, texturePaths);
			currentTextureCount = static_cast<uint32_t>(imageSystem->getTextureCount());
		}
		
		std::cout << "ImageSystem initialized with " << imageSystem->getTextureCount() << " textures" << std::endl;
		std::cout << "Pipeline will be created with texture array size: " << currentTextureCount << std::endl;
	}

	void Renderer::setupDescriptors()
	{
		if (!imageSystem) {
			throw std::runtime_error("ImageSystem must be initialized before setting up descriptors (call initializeImageSystem first)");
		}

		int numObjects = sceneLoader.getObjectCount();

		// Create Descriptor Pools using dynamic texture count
		descriptorPool = AvengDescriptorPool::Builder(engineDevice)
			.setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * 100)  // Increased for animation descriptor sets
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 10)
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * currentTextureCount)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, SwapChain::MAX_FRAMES_IN_FLIGHT * 10)
			.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 10)  // NEW: For animation SSBOs
			.build();

		// So we can access our pool where other descriptors are required
		renderData.avengDescriptorPool = descriptorPool->getPool();

		// Resize buffer vectors
		u_GlobalBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		u_ObjBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		u_LightsBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		// Resize descriptor set vectors
		globalDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		objectDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		lightsDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		// Create Buffers using VMA
		// VMA_MEMORY_USAGE_AUTO: Let VMA choose optimal memory type
		// VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT: CPU will write to this buffer frequently  
		// VMA_ALLOCATION_CREATE_MAPPED_BIT: Keep buffer persistently mapped for performance

		// New TODO:
		// UBOs : 
		//		avengBasicUboBuffer, 
		//		avengAnimUboBuffer
		// Ssbos: 
		//		basicSsbo0: 1 update per frame 
		//		skinningSsbo1, 
		//		skinningSsbo2
		// Image Sampler

		for (int i = 0; i < u_GlobalBuffers.size(); i++) {
			u_GlobalBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(GlobalUbo), 1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			u_GlobalBuffers[i]->map();
		}

		for (int i = 0; i < u_LightsBuffers.size(); i++) {
			u_LightsBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(LightsUbo), 1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			u_LightsBuffers[i]->map();
		}

		for (int i = 0; i < u_ObjBuffers.size(); i++) {
			u_ObjBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				calculateDynamicUBOStride(), numObjects, // HARDCODED - Dynamic UBO size
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				calculateDynamicUBOStride(), // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			u_ObjBuffers[i]->map();
		}

		/*
			Note to self: These buffers are guaranteed to be mapped to a location in device memory
						  with enough space allocated to support their resources.
		*/

		// Verify VMA allocation is working
		//std::cout << "[Info]  VMA Buffer Allocation Verification:" << std::endl;
		//std::cout << "[Info]  Global Buffer [0] using VMA: " << (u_GlobalBuffers[0]->isUsingVMA() ? "YES" : "NO") << std::endl;
		//std::cout << "[Info]  Lights Buffer [0] using VMA: " << (u_LightsBuffers[0]->isUsingVMA() ? "YES" : "NO") << std::endl;
		//std::cout << "[Info]  Object Buffer [0] using VMA: " << (u_ObjBuffers[0]->isUsingVMA() ? "YES" : "NO") << std::endl;

		// Check memory budget after buffer creation
		engineDevice.printMemoryStats();
		if (engineDevice.isMemoryPressureHigh()) {
			std::cout << "[WARNING] High memory pressure detected!" << std::endl;
		}

		avengTextureDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, currentTextureCount)
			.build();

		avengDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
			.build();

		avengSkinningDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
			.addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1)
			.build();

		computeTransformDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)
			.build();

		computeMatrixMultDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)
			.build();

		computeMatPerModelDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)
			.addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT, 1)
			.build();

		avengBasicLightingDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.build();

		// Punt these to the renderData. This is an architectural workaround due to AvengDescriptorSetLayout not being easily available
		renderData.rdAvengTextureDescriptorLayout = avengTextureDescriptorSetLayout->getDescriptorSetLayout();
		renderData.rdAvengDescriptorLayout = avengDescriptorSetLayout->getDescriptorSetLayout();
		renderData.rdAvengSkinningDescriptorLayout = avengSkinningDescriptorSetLayout->getDescriptorSetLayout();
		renderData.rdAvengComputeTransformDescriptorLayout = computeTransformDescriptorLayout->getDescriptorSetLayout();
		renderData.rdAvengComputeMatrixMultDescriptorLayout = computeMatrixMultDescriptorLayout->getDescriptorSetLayout();
		renderData.rdAvengComputeMatrixMultPerModelDescriptorLayout = computeMatPerModelDescriptorSetLayout->getDescriptorSetLayout();
		renderData.rdAvengBasicLightingDescriptorLayout = avengBasicLightingDescriptorSetLayout->getDescriptorSetLayout();

		//// Initialize animation rendering system only if it exists
		//if (animationSystem) {
		//	animationSystem->initializeDescriptors(*descriptorPool, SwapChain::MAX_FRAMES_IN_FLIGHT);
		//}

		// Why auto?
		auto imageInfo = imageSystem->descriptorInfoForAllImages();

		// Write the descriptor sets that are ready to go
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			auto globalBufferInfo = u_GlobalBuffers[i]->descriptorInfo(sizeof(GlobalUbo), 0);
			AvengDescriptorSetWriter(*globalDescriptorSetLayout, *descriptorPool)
				.writeBuffer(0, &globalBufferInfo)
				.writeImage(1, imageInfo.data(), imageInfo.size())
				.build(globalDescriptorSets[i]);

			auto objBufferInfo = u_ObjBuffers[i]->descriptorInfo(calculateDynamicUBOStride(), 0);
			AvengDescriptorSetWriter(*objDescriptorSetLayout, *descriptorPool)
				.writeBuffer(0, &objBufferInfo)
				.build(objectDescriptorSets[i]);

			auto lightsBufferInfo = u_LightsBuffers[i]->descriptorInfo(sizeof(LightsUbo), 0);
			AvengDescriptorSetWriter(*lightsDescriptorSetLayout, *descriptorPool)
				.writeBuffer(0, &lightsBufferInfo)
				.build(lightsDescriptorSets[i]);
		}
	
	}

	void Renderer::initializePointLightSystem()
	{
		if (!lightsDescriptorSetLayout) {
			throw std::runtime_error("Descriptor set layouts must be created before initializing PointLightSystem (call setupDescriptors first)");
		}

		std::cout << "Initializing PointLightSystem" << std::endl;

		// Initialize point light system using existing descriptor set layouts
		pointLightSystem.initialize(
			getSwapChainRenderPass(),
			globalDescriptorSetLayout->getDescriptorSetLayout(),
			lightsDescriptorSetLayout->getDescriptorSetLayout()
		);

		std::cout << "PointLightSystem initialized" << std::endl;
	}

	void Renderer::updateFrameData(const glm::mat4& projection, const glm::mat4& view)
	{

		// Prepare frame data
		u_GlobalData.projection = projection;
		u_GlobalData.view = view;
		u_GlobalData.renderMode = static_cast<int>(getObjectRenderMode());

		// Update global uniform buffer
		u_GlobalBuffers[currentFrameIndex]->writeToBuffer(&u_GlobalData);
		u_GlobalBuffers[currentFrameIndex]->flush();

		// Update lights uniform buffer
		u_LightsBuffers[currentFrameIndex]->writeToBuffer(&u_LightsData);
		u_LightsBuffers[currentFrameIndex]->flush();
	}

	//void Renderer::updateAnimationData(const std::vector<std::shared_ptr<AssimpInstance>>& instances, float deltaTime)
	//{
	//	// Delegate to animation system (only if it exists)
	//	//if (animationSystem) {
	//	//	animationSystem->updateAnimationData(instances, deltaTime, currentFrameIndex);
	//	//}
	//}

	void Renderer::dispatchAnimationCompute(uint32_t vertexCount)
	{
		// Delegate to animation system (only if it exists)
		//if (animationSystem) {
		//	animationSystem->dispatchAnimationCompute(getCurrentCommandBuffer(), vertexCount, 
		//											 pipelineLayout, currentFrameIndex);
		//}
	}

	void Renderer::renderAnimatedModels(const std::vector<std::shared_ptr<AssimpInstance>>& instances)
	{

		// DEBUG: Completely disable compute shader to test raw geometry
		/*
		if (!animatedInstances.empty()) {
			// Calculate total vertices for compute dispatch
			uint32_t totalVertices = 0;
			for (const auto& instance : animatedInstances) {
				const auto& meshes = instance->getModel()->getModelMeshes();
				for (const auto& mesh : meshes) {
					totalVertices += static_cast<uint32_t>(mesh.vertices.size());
				}
			}

			if (totalVertices > 0) {
				// Dispatch compute shader outside render pass
				renderer.dispatchAnimationCompute(totalVertices);
			}
		}
		*/

		//// Delegate to animation system (only if it exists)
		//if (animationSystem) {
		//	animationSystem->renderAnimatedModels(getCurrentCommandBuffer(), instances, pipelineLayout,
		//										 pipelineManager.get(), static_cast<int>(currentObjectMode), 
		//										 currentFrameIndex);
		//}
	}

	void Renderer::renderObjects(const std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>>& objectData)
	{
		auto commandBufferGfx = getCurrentCommandBufferGraphics();

		// Bind pipeline based on current render mode
		//GFXPipeline* activePipeline = nullptr;
		
		// PRIMARY: Use JSON-configured pipeline manager
		//if (pipelineManager) {
			//activePipeline = pipelineManager->getPipeline(static_cast<int>(currentObjectMode));
			//if (activePipeline) {
			//	activePipeline->bind(commandBuffer);
			//}
			pipelineManager->getPipeline(static_cast<int>(currentObjectMode))->bind(commandBufferGfx);
		//}
		
		//if (!activePipeline) {
		//	std::cerr << "WARNING: No pipeline found for mode " << static_cast<int>(currentObjectMode) 
		//	         << ". Check PipelineConfig.json or add missing pipeline definition." << std::endl;
		//	
		//	// DEPRECATED FALLBACKS (should not be reached in production)
		//	if (static_cast<size_t>(currentObjectMode) < objectPipelines.size() && objectPipelines[static_cast<size_t>(currentObjectMode)]) {
		//		std::cerr << "Using deprecated hardcoded pipeline fallback" << std::endl;
		//		objectPipelines[static_cast<size_t>(currentObjectMode)]->bind(commandBuffer);
		//	}
		//	else if (gfxPipeline) {
		//		std::cerr << "Using legacy gfxPipeline fallback - this should be removed!" << std::endl;
		//		gfxPipeline->bind(commandBuffer);
		//	}
		//	else {
		//		throw std::runtime_error("No pipelines available - system misconfigured!");
		//	}
		//}

		// Bind global descriptor set (set 0)
		vkCmdBindDescriptorSets(commandBufferGfx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, 1, &globalDescriptorSets[currentFrameIndex], 0, nullptr);

		// Bind lights descriptor set (set 2)
		vkCmdBindDescriptorSets(commandBufferGfx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			2, 1, &lightsDescriptorSets[currentFrameIndex], 0, nullptr);

		// Render each object
		for (size_t i = 0; i < objectData.size(); ++i) {
			const auto& [objUniform, modelMatrix, normalMatrix, model] = objectData[i];

			// Update object buffer
			u_ObjBuffers[currentFrameIndex]->writeToIndex(&objUniform, i);
			u_ObjBuffers[currentFrameIndex]->flushIndex(i);
			auto descriptorInfo = u_ObjBuffers[currentFrameIndex]->descriptorInfoForIndex(i);
			uint32_t dynamicOffset = static_cast<uint32_t>(descriptorInfo.offset);

			// Bind object descriptor set (set 1)
			vkCmdBindDescriptorSets(commandBufferGfx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
				1, 1, &objectDescriptorSets[currentFrameIndex], 1, &dynamicOffset);

			PushConstantData pushData{ modelMatrix, normalMatrix, 0 };

			vkCmdPushConstants(commandBufferGfx, pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT,  // Only vertex stage uses push constants
				0, sizeof(PushConstantData), &pushData);

			// Bind and draw model
			model->bind(commandBufferGfx);
			model->draw(commandBufferGfx);
		}
	}

	void Renderer::renderLights()
	{
		pointLightSystem.render(
			globalDescriptorSets[currentFrameIndex], 
			lightsDescriptorSets[currentFrameIndex], 
			getCurrentCommandBufferGraphics(), 
			u_LightsData.numLights);
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

	void Renderer::renderObjectsInstanced(const std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>>& objectData)
	{

		// Pro Tip: structured bindings won't hurt performance as long as your lvalue is const auto&
		// E.g.
		//	for(const auto& [objUniform, modelMatrix, normalMatrix, model] : objectData) ...
		//

		if (!instancedRenderingEnabled || objectData.empty()) { // Strange that we're considering empty object data at all
			// Fallback to traditional rendering
			renderObjects(objectData);
			return;
		}

		auto commandBufferGfx = getCurrentCommandBufferGraphics();

		//const std::unordered_map modelCountCache = sceneLoader.getModelCountCache();
		//const std::unordered_map modelCache = sceneLoader.getModelCache();

		// Clear existing batches for this frame
		for (auto& [model, batch] : renderBatches) {
			batch->instances.clear();
		}

		// Group objects by model
		for (const auto& [objUniform, modelMatrix, normalMatrix, model] : objectData) {
			// Create batch if it doesn't exist
			if (renderBatches.find(model) == renderBatches.end()) {
				renderBatches[model] = std::make_unique<RenderBatch>(model);
			}

			// Add instance data to the batch
			InstanceData instanceData{};
			instanceData.modelMatrix = modelMatrix;
			instanceData.normalMatrix = normalMatrix;
			instanceData.textureIndex = objUniform.texIndex;

			//std::cout << "Adding Instance Data for: " << model->path << std::endl;

			renderBatches[model]->instances.push_back(instanceData);
		}

		// Bind pipeline based on current render mode
		// Use instanced pipeline variants (IDs 10, 11, 12 for STANDARD, WIREFRAME, DISTORTED respectively)
		GFXPipeline* activePipeline = nullptr;
		
		if (pipelineManager) {
			int instancedPipelineId = static_cast<int>(currentObjectMode) + 10;  // Convert to instanced pipeline ID
			activePipeline = pipelineManager->getPipeline(instancedPipelineId);  // This is benign AI logic. Just build a method that chooses based on constants
			if (activePipeline) {
				activePipeline->bind(commandBufferGfx);
			}
		}
		
		if (!activePipeline) {
			std::cerr << "WARNING: No instanced pipeline found for mode " << static_cast<int>(currentObjectMode) 
					  << " (instanced ID " << (static_cast<int>(currentObjectMode) + 10) << ")" << std::endl;
			std::cerr << "Falling back to standard rendering" << std::endl;
			renderObjects(objectData); // Fallback
			return;
		}

		// Bind global descriptor set (set 0)
		vkCmdBindDescriptorSets(commandBufferGfx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, 1, &globalDescriptorSets[currentFrameIndex], 0, nullptr);

		// Bind lights descriptor set (set 2)
		vkCmdBindDescriptorSets(commandBufferGfx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			2, 1, &lightsDescriptorSets[currentFrameIndex], 0, nullptr);

		// Render each batch
		uint32_t totalInstancesRendered = 0;
		uint32_t batchesRendered = 0;
		
		for (auto& [model, batch] : renderBatches) {
			if (batch->instances.empty()) continue;

			//std::cout << "RenderBatch Model: " << model->path << std::endl;

			uint32_t instanceCount = static_cast<uint32_t>(batch->instances.size());
			//std::cout << "Instance COUNT: " << instanceCount << std::endl;

			// Update instance buffer for this batch
			updateInstanceBuffer(*batch);

			/*
			 * TODO - Reintroduce Push Constants
			 */

			// Bind model vertex buffers AND instance buffer
			// The instance buffer contains modelMatrix, normalMatrix, and textureIndex for each instance
			model->bindInstanced(commandBufferGfx, batch->instanceBuffer->getBuffer());

			// Draw all instances in a single draw call
			// The shader will use gl_InstanceIndex to fetch the correct instance data from the instance buffer
			model->drawInstanced(commandBufferGfx, instanceCount, 0);

			totalInstancesRendered += instanceCount;
			batchesRendered++;
		}

#if _DEBUG
		//// Performance analysis
		//float efficiency = (float)totalInstancesRendered / (float)batchesRendered;
		//if (efficiency > 2.0f) {
		//	std::cout << "Instancing is BENEFICIAL! Avg " << efficiency << " instances per batch" << std::endl;
		//}
		//else if (efficiency > 1.0f) {
		//	std::cout << "Instancing provides MINOR benefit. Avg " << efficiency << " instances per batch" << std::endl;
		//}
		//if (efficiency < 1.0f) {
		//	std::cout << "Instancing provides NO benefit (efficiency: " << efficiency << ")" << std::endl;
		//	std::cout << "Auto-switching to traditional rendering for better performance" << std::endl;

		//	// Automatically disable instancing for inefficient scenes
		//	static int inefficientFrameCount = 0;
		//	inefficientFrameCount++;
		//	if (inefficientFrameCount >= 5) {
		//		std::cout << "Automatically disabling instanced rendering - scene not suitable" << std::endl;
		//		instancedRenderingEnabled = false;
		//	}
		//}
		//else {
		//	instancedRenderingEnabled = true;
		//}
#endif
	}

	void Renderer::updateInstanceBuffer(RenderBatch& batch)
	{
		if (batch.instances.empty()) return;

		uint32_t instanceCount = static_cast<uint32_t>(batch.instances.size());
		
		// Create or resize instance buffer if needed
		if (!batch.instanceBuffer || 
		    batch.instanceBuffer->getInstanceCount() < instanceCount) {
			
			std::cout << "Creating instance buffer for " << instanceCount << " instances" << std::endl;
			
			// Create new instance buffer with VMA
			batch.instanceBuffer = std::make_unique<AvengBuffer>(
				engineDevice,
				sizeof(InstanceData),
				instanceCount,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
			);
			
			batch.instanceBuffer->map();
		}

		// Update instance buffer with current instance data
		batch.instanceBuffer->writeToBuffer(batch.instances.data(), sizeof(InstanceData) * instanceCount);

		// TODO - Does this need to be flushed?
		batch.instanceBuffer->flush();
	}

	void Renderer::createPipelineLayout()
	{
		if (! lightsDescriptorSetLayout ||
			! textureDescriptorSetLayout ||
			! avengDescriptorSetLayout ||
			! avengAnimDescriptorSetLayout ||
			! computeTransformDescriptorSetLayout ||
			! computeMatGlobalDescriptorSetLayout ||
			! computeMatInstanceDescriptorSetLayout
		) {
			throw std::runtime_error("Descriptor set layouts must be created before pipeline layout (call setupDescriptors first)");
		}

		// Create descriptor set layouts array using stored layouts
		// Size depends on whether animation system is enabled
		size_t descriptorSetCount = /* animationSystem ? 4 : */ 3;
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts(descriptorSetCount);
		descriptorSetLayouts[0] = globalDescriptorSetLayout->getDescriptorSetLayout();
		descriptorSetLayouts[1] = objDescriptorSetLayout->getDescriptorSetLayout();
		descriptorSetLayouts[2] = lightsDescriptorSetLayout->getDescriptorSetLayout();
		
		//if (animationSystem) {
		//	descriptorSetLayouts[3] = animationSystem->getAnimationDescriptorSetLayout();  // Animation system descriptor set
		//}

		// Push constant range - unified structure for all pipelines
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;  // Only vertex shaders use push constants
		pushConstantRange.offset = 0;
		pushConstantRange.size = 132; // Unified: 2 mat4 + int32_t = 132 bytes (within 128-byte limit)

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetCount);
		pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		if (vkCreatePipelineLayout(engineDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
			throw std::runtime_error("failed to create pipeline layout!");
		}
		
		// Create animation compute pipeline now that we have the pipeline layout (if animation system exists)
	/*	if (animationSystem) {
			animationSystem->createAnimationComputePipeline(pipelineLayout);
		}*/
	}

	void Renderer::createPipelines()
	{
		if (!imageSystem) {
			throw std::runtime_error("ImageSystem must be initialized before creating pipelines");
		}

		// Create pipelines now that we have the correct texture count
		if (!pipelineCreated) {
			createPipelineLayout();
			
			// PRIMARY: JSON-configured pipeline system
			pipelineManager = std::make_unique<PipelineConfigManager>(engineDevice);
			try {
				std::cout << "Loading pipelines from JSON configuration..." << std::endl;
				pipelineManager->loadPipelineConfig("Midnight/Core/Renderer/PipelineConfig.json");
				pipelineManager->createPipelines(getSwapChainRenderPass(), pipelineLayout, currentTextureCount);
				std::cout << "JSON pipeline system successfully initialized!" << std::endl;
			} catch (const std::exception& e) {
				std::cerr << "Failed to load pipeline config: " << e.what() << std::endl;
				std::cerr << "Creating deprecated fallback pipelines..." << std::endl;
				
				// DEPRECATED: Create legacy pipelines as fallback
				//createPipeline();         // Legacy gfxPipeline
				//createObjectPipelines();  // Hardcoded array
			}
			
			//createPostProcessPipelines();  // Post-processing pipelines (placeholder)
			pipelineCreated = true;
			std::cout << "Pipeline initialization complete" << std::endl;
		}
	}

	
	//void Renderer::createPostProcessPipelines()
	//{
	//	std::cout << "Creating post-processing pipelines..." << std::endl;

	//	// For now, create placeholder - full post-processing setup would require:
	//	// 1. Offscreen render targets
	//	// 2. Full-screen quad rendering
	//	// 3. Screen-space effect shaders

	//	// Resize pipeline vector to match enum size  
	//	//postProcessPipelines.resize(static_cast<size_t>(PostProcessMode::CHROMATIC_ABERRATION) + 1);

	//	// TODO: Implement full post-processing system
	//	// This would include:
	//	// - Creating offscreen framebuffers
	//	// - Post-process specific pipeline layouts
	//	// - Full-screen quad geometry generation
	//	// - Screen-space effect shaders (toxic_cloud.frag, night_vision.frag, etc.)

	//	std::cout << "Post-processing system placeholder created (full implementation needed)" << std::endl;
	//}

	bool Renderer::reloadPipelineConfig(const std::string& configPath)
	{
		if (!pipelineManager) {
			std::cerr << "Pipeline manager not initialized" << std::endl;
			return false;
		}

		try {
			std::string path = configPath.empty() ? "Midnight/Core/Renderer/PipelineConfig.json" : configPath;
			std::cout << "Reloading pipeline configuration from: " << path << std::endl;
			
			pipelineManager->loadPipelineConfig(path);
			pipelineManager->createPipelines(getSwapChainRenderPass(), pipelineLayout, currentTextureCount);
			
			std::cout << "Pipeline configuration reloaded successfully!" << std::endl;
			return true;
		} catch (const std::exception& e) {
			std::cerr << "Failed to reload pipeline config: " << e.what() << std::endl;
			return false;
		}
	}

	std::vector<std::string> Renderer::getAvailablePipelines() const
	{
		if (pipelineManager) {
			return pipelineManager->getPipelineNames();
		}
		return {};
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

		/* node transformation */
		vkCmdBindPipeline(renderData.rdComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAssimpComputeTransformPipeline);
		vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAssimpComputeTransformaPipelineLayout, 0, 1, &renderData.rdAssimpComputeTransformDescriptorSet, 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(renderData.rdComputeCommandBuffer, renderData.rdAssimpComputeTransformaPipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &mComputeModelData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		vkCmdDispatch(renderData.rdComputeCommandBuffer, numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

		/* memroy barrier between the compute shaders
		 * wait for TRS buffer to be written  */
		VkBufferMemoryBarrier trsBufferBarrier{};
		trsBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		trsBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		trsBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		trsBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		trsBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		trsBufferBarrier.buffer = mShaderTRSMatrixBuffer.buffer;
		trsBufferBarrier.offset = 0;
		trsBufferBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(renderData.rdComputeCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
			&trsBufferBarrier, 0, nullptr);

		/* matrix multiplication */
		vkCmdBindPipeline(renderData.rdComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAssimpComputeMatrixMultPipeline);

		VkDescriptorSet& modelDescriptorSet = model->getMatrixMultDescriptorSet();
		std::vector<VkDescriptorSet> computeSets = { renderData.rdAssimpComputeMatrixMultDescriptorSet, modelDescriptorSet };
		vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAssimpComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(computeSets.size()), computeSets.data(), 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(renderData.rdComputeCommandBuffer, renderData.rdAssimpComputeMatrixMultPipelineLayout,
			VK_SHADER_STAGE_COMPUTE_BIT, 0, static_cast<uint32_t>(sizeof(VkComputePushConstants)), &mComputeModelData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		vkCmdDispatch(renderData.rdComputeCommandBuffer, numberOfBones, static_cast<uint32_t>(std::ceil(numInstances / 32.0f)), 1);

		/* memroy barrier after compute shader
		 * wait for bone matrix buffer to be written  */
		VkBufferMemoryBarrier boneMatrixBufferBarrier{};
		boneMatrixBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
		boneMatrixBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		boneMatrixBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		boneMatrixBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		boneMatrixBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		boneMatrixBufferBarrier.buffer = mShaderBoneMatrixBuffer.buffer;
		boneMatrixBufferBarrier.offset = 0;
		boneMatrixBufferBarrier.size = VK_WHOLE_SIZE;

		vkCmdPipelineBarrier(renderData.rdComputeCommandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
			&boneMatrixBufferBarrier, 0, nullptr);
	}

} // NS