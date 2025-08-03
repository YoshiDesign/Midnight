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
		renderData.engineDevice = &engineDevice;

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

		setupDescriptors();


		// Initialize PointLightSystem now that descriptor layouts are created
		initializePointLightSystem();

		//animationSystem = std::make_unique<AnimationRenderingSystem>(engineDevice);

		editor.init(window, aveng_swapchain.get());

	}

	Renderer::~Renderer()
	{
		std::cout << "Destroying Renderer..." << std::endl;
		//renderBatches.clear();
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
		commandBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		// Command buffer memory is allocated from a command pool
		allocInfo.commandPool = engineDevice.commandPool();
		allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

		if (vkAllocateCommandBuffers(engineDevice.device(), &allocInfo, commandBuffers.data()) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to allocate command buffers.");
		}

	}

	void Renderer::freeCommandBuffers()
	{
		vkFreeCommandBuffers(
			engineDevice.device(),
			engineDevice.commandPool(),
			static_cast<uint32_t>(commandBuffers.size()),
			commandBuffers.data()
		);

		commandBuffers.clear();
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

		commandBuffer = getCurrentCommandBuffer();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("Command Buffer failed to begin recording.");
		}

		/**
		 * Next Step, begin the render pass
		 */

		// Clear Color for now
		glm::vec3 clear_color = glm::vec3(0.001f, 0.008f, 0.06f); // Cool, dark midnight blue
		beginSwapChainRenderPass(commandBuffer, clear_color);

	}

	// 
	void  Renderer::endFrame()
	{
		assert(isFrameStarted && "Can't call endFrame while frame is not in progress.");

		
		commandBuffer = getCurrentCommandBuffer();
		endSwapChainRenderPass(commandBuffer);
		if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
		{
			throw std::runtime_error("Failed to record command buffer.");
		}


		// Submit to graphics queue while handling cpu and gpu sync, executing the command buffers
		auto result = aveng_swapchain->submitCommandBuffers(&commandBuffer, &currentImageIndex);

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


	void  Renderer::beginSwapChainRenderPass(VkCommandBuffer commandBuffer, glm::vec3 rgb)
	{
	
		assert(isFrameStarted && "Can't call beginSwapChain if frame is not in progress.");
		assert(
			commandBuffer == getCurrentCommandBuffer() && 
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
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	}

	void  Renderer::endSwapChainRenderPass(VkCommandBuffer commandBuffer)
	{
		assert(isFrameStarted && "Can't call endSwapChain if frame is not in progress.");
		assert(
			commandBuffer == getCurrentCommandBuffer() &&
			"Can't end render pass on command buffer from a different frame");
		vkCmdEndRenderPass(commandBuffer);
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

		auto imageInfo = imageSystem->descriptorInfoForAllImages();
		int numObjects = sceneLoader.getObjectCount();

		// Create Descriptor Pools using dynamic texture count
		descriptorPool = AvengDescriptorPool::Builder(engineDevice)
			.setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * 5)  // Increased for animation descriptor sets
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 3)
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * currentTextureCount)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, SwapChain::MAX_FRAMES_IN_FLIGHT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 5)  // NEW: For animation SSBOs
			.build();

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

		// Verify VMA allocation is working
		std::cout << "[Info]  VMA Buffer Allocation Verification:" << std::endl;
		std::cout << "[Info]  Global Buffer [0] using VMA: " << (u_GlobalBuffers[0]->isUsingVMA() ? "YES" : "NO") << std::endl;
		std::cout << "[Info]  Lights Buffer [0] using VMA: " << (u_LightsBuffers[0]->isUsingVMA() ? "YES" : "NO") << std::endl;
		std::cout << "[Info]  Object Buffer [0] using VMA: " << (u_ObjBuffers[0]->isUsingVMA() ? "YES" : "NO") << std::endl;

		// Check memory budget after buffer creation
		engineDevice.printMemoryStats();
		if (engineDevice.isMemoryPressureHigh()) {
			std::cout << "[WARNING] High memory pressure detected!" << std::endl;
		}

		// Create Descriptor Set Layouts (stored as members for reuse)
		globalDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, currentTextureCount)
			.build();

		objDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.build();

		lightsDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.build();
			
		//// Initialize animation rendering system only if it exists
		//if (animationSystem) {
		//	animationSystem->initializeDescriptors(*descriptorPool, SwapChain::MAX_FRAMES_IN_FLIGHT);
		//}

		// Write descriptor sets
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

		// Create pipelines now that descriptor layouts are ready
		createPipelines();
	
	}

	void Renderer::initializePointLightSystem()
	{
		if (!globalDescriptorSetLayout || !lightsDescriptorSetLayout) {
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

	void Renderer::updateAnimationData(const std::vector<std::shared_ptr<AssimpInstance>>& instances, float deltaTime)
	{
		// Delegate to animation system (only if it exists)
		//if (animationSystem) {
		//	animationSystem->updateAnimationData(instances, deltaTime, currentFrameIndex);
		//}
	}

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
		auto commandBuffer = getCurrentCommandBuffer();

		// Bind pipeline based on current render mode
		GFXPipeline* activePipeline = nullptr;
		
		// PRIMARY: Use JSON-configured pipeline manager
		if (pipelineManager) {
			activePipeline = pipelineManager->getPipeline(static_cast<int>(currentObjectMode));
			if (activePipeline) {
				activePipeline->bind(commandBuffer);
			}
		}
		
		if (!activePipeline) {
			std::cerr << "WARNING: No pipeline found for mode " << static_cast<int>(currentObjectMode) 
			         << ". Check PipelineConfig.json or add missing pipeline definition." << std::endl;
			
			// DEPRECATED FALLBACKS (should not be reached in production)
			if (static_cast<size_t>(currentObjectMode) < objectPipelines.size() && objectPipelines[static_cast<size_t>(currentObjectMode)]) {
				std::cerr << "Using deprecated hardcoded pipeline fallback" << std::endl;
				objectPipelines[static_cast<size_t>(currentObjectMode)]->bind(commandBuffer);
			}
			else if (gfxPipeline) {
				std::cerr << "Using legacy gfxPipeline fallback - this should be removed!" << std::endl;
				gfxPipeline->bind(commandBuffer);
			}
			else {
				throw std::runtime_error("No pipelines available - system misconfigured!");
			}
		}

		// Bind global descriptor set (set 0)
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, 1, &globalDescriptorSets[currentFrameIndex], 0, nullptr);

		// Bind lights descriptor set (set 2)
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
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
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
				1, 1, &objectDescriptorSets[currentFrameIndex], 1, &dynamicOffset);

			PushConstantData pushData{ modelMatrix, normalMatrix, 0 };

			vkCmdPushConstants(commandBuffer, pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT,  // Only vertex stage uses push constants
				0, sizeof(PushConstantData), &pushData);

			// Bind and draw model
			model->bind(commandBuffer);
			model->draw(commandBuffer);
		}
	}

	void Renderer::renderLights()
	{
		pointLightSystem.render(globalDescriptorSets[currentFrameIndex], 
			lightsDescriptorSets[currentFrameIndex], getCurrentCommandBuffer(), u_LightsData.numLights);
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

		/**
		* Pro Tip: structured bindings won't hurt performance as long as your lvalue is const auto&
		* E.g.
		*	for(const auto& [objUniform, modelMatrix, normalMatrix, model] : objectData) ...
		*/

//		if (!instancedRenderingEnabled || objectData.empty()) {
//			// Fallback to traditional rendering
//			renderObjects(objectData);
//			return;
//		}
//
//		auto commandBuffer = getCurrentCommandBuffer();
//
//		// Clear existing batches for this frame
//		for (auto& [model, batch] : renderBatches) {
//			batch->instances.clear();
//		}
//
//		// Group objects by model
//		for (const auto& [objUniform, modelMatrix, normalMatrix, model] : objectData) {
//			// Create batch if it doesn't exist
//			if (renderBatches.find(model) == renderBatches.end()) {
//				renderBatches[model] = std::make_unique<RenderBatch>(model);
//			}
//
//			// Add instance data to the batch
//			InstanceData instanceData{};
//			instanceData.modelMatrix = modelMatrix;
//			instanceData.normalMatrix = normalMatrix;
//			instanceData.textureIndex = objUniform.texIndex;
//
//			renderBatches[model]->instances.push_back(instanceData);
//		}
//
//		// Bind pipeline based on current render mode
//		GFXPipeline* activePipeline = nullptr;
//		
//		if (pipelineManager) {
//			activePipeline = pipelineManager->getPipeline(static_cast<int>(currentObjectMode));
//			if (activePipeline) {
//				activePipeline->bind(commandBuffer);
//			}
//		}
//		
//		if (!activePipeline) {
//			std::cerr << "WARNING: No pipeline found for instanced mode " << static_cast<int>(currentObjectMode) << std::endl;
//			renderObjects(objectData); // Fallback
//			return;
//		}
//
//		// Bind global descriptor set (set 0)
//		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
//			0, 1, &globalDescriptorSets[currentFrameIndex], 0, nullptr);
//
//		// Bind lights descriptor set (set 2)
//		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
//			2, 1, &lightsDescriptorSets[currentFrameIndex], 0, nullptr);
//
//		// Render each batch with GLOBAL index tracking
//		uint32_t totalInstancesRendered = 0;
//		uint32_t batchesRendered = 0;
//		uint32_t globalInstanceIndex = 0;  // Track global index across ALL batches
//		
//		for (auto& [model, batch] : renderBatches) {
//			if (batch->instances.empty()) continue;
//
//			uint32_t instanceCount = static_cast<uint32_t>(batch->instances.size());
//			
//			//for (uint32_t j = 0; j < instanceCount; ++j) {
//			//	std::cout << batch->instances[j].textureIndex << " ";
//			//}
//
//			// Update instance buffer for this batch
//			updateInstanceBuffer(*batch);
//
//			// Bind model vertex and index buffers
//			model->bind(commandBuffer);
//
//			/**
//			* TODO - Plenty of things to do here.
//			* 1. The Model class should really be aware of how many instances of itself there are, managing adds/removes at runtime. Instead we're counting them here
//			* 2. 
//			*/
//
//			// Render each instance with correct texture binding using GLOBAL index
//			for (uint32_t i = 0; i < instanceCount; ++i) {
//				// Update object buffer with correct texture for this instance
//				ObjectUniformData objUniform{ batch->instances[i].textureIndex };
//				
//				// Use globalInstanceIndex instead of local i to prevent buffer overwrites
//				u_ObjBuffers[currentFrameIndex]->writeToIndex(&objUniform, globalInstanceIndex + i);
//				u_ObjBuffers[currentFrameIndex]->flushIndex(globalInstanceIndex + i); // TODO - Does this need to be flushed?
//				auto descriptorInfo = u_ObjBuffers[currentFrameIndex]->descriptorInfoForIndex(globalInstanceIndex + i);
//				uint32_t dynamicOffset = static_cast<uint32_t>(descriptorInfo.offset);
//
//				// Bind object descriptor set (set 1) with correct texture for this instance
//				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
//					1, 1, &objectDescriptorSets[currentFrameIndex], 1, &dynamicOffset);
//
//				// Update push constants for each instance
//				PushConstantData pushData{ 
//					batch->instances[i].modelMatrix,
//					batch->instances[i].normalMatrix,
//					0
//				};
//
//				vkCmdPushConstants(commandBuffer, pipelineLayout,
//					VK_SHADER_STAGE_VERTEX_BIT,  // Only vertex stage uses push constants
//					0, sizeof(PushConstantData), &pushData);
//
//			}
//			// Draw single instance with correct texture
//			model->drawInstanced(commandBuffer, instanceCount, globalInstanceIndex);
//			// Advance global index for next batch
//			globalInstanceIndex += instanceCount;
//			totalInstancesRendered += instanceCount;
//			batchesRendered++;
//		}
//
//#if _DEBUG
//		/**
//		* ToDo - This is not implemented properly. You'll likely want to count the number of instances of an object BEFORE any of this function's code executes.
//		*/
//		// Performance analysis
//		float efficiency = (float)totalInstancesRendered / (float)batchesRendered;
//		//if (efficiency > 2.0f) {
//		//	std::cout << "Instancing is BENEFICIAL! Avg " << efficiency << " instances per batch" << std::endl;
//		//}
//		//else if (efficiency > 1.0f) {
//		//	std::cout << "Instancing provides MINOR benefit. Avg " << efficiency << " instances per batch" << std::endl;
//		//}
//		if (efficiency < 1.0f) {
//			std::cout << "Instancing provides NO benefit (efficiency: " << efficiency << ")" << std::endl;
//			std::cout << "Auto-switching to traditional rendering for better performance" << std::endl;
//
//			// Automatically disable instancing for inefficient scenes
//			static int inefficientFrameCount = 0;
//			inefficientFrameCount++;
//			if (inefficientFrameCount >= 5) {
//				std::cout << "Automatically disabling instanced rendering - scene not suitable" << std::endl;
//				instancedRenderingEnabled = false;
//			}
//		}
//		else {
//			instancedRenderingEnabled = true;
//		}
//#endif
	}

	//void Renderer::updateInstanceBuffer(RenderBatch& batch)
	//{
	//	if (batch.instances.empty()) return;

	//	uint32_t instanceCount = static_cast<uint32_t>(batch.instances.size());
	//	
	//	// Create or resize instance buffer if needed
	//	if (!batch.instanceBuffer || 
	//	    batch.instanceBuffer->getInstanceCount() < instanceCount) {
	//		
	//		std::cout << "Creating instance buffer for " << instanceCount << " instances" << std::endl;
	//		
	//		// Create new instance buffer with VMA
	//		batch.instanceBuffer = std::make_unique<AvengBuffer>(
	//			engineDevice,
	//			sizeof(InstanceData),
	//			instanceCount,
	//			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	//			VMA_MEMORY_USAGE_AUTO,
	//			1, // minOffsetAlignment
	//			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
	//		);
	//		
	//		batch.instanceBuffer->map();
	//	}

	//	// Update instance buffer with current instance data
	//	batch.instanceBuffer->writeToBuffer(batch.instances.data(), 
	//		sizeof(InstanceData) * instanceCount);

	//	// TODO - Does this need to be flushed?
	//	batch.instanceBuffer->flush();
	//}

	void Renderer::createPipelineLayout()
	{
		if (!globalDescriptorSetLayout || !objDescriptorSetLayout || !lightsDescriptorSetLayout) {
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


	//void Renderer::testAnimationSystem()
	//{
	//	std::cout << "\n===ANIMATION DEBUG: ANIMATED CUBE TEST (CPU ANIMATION) ===" << std::endl;
	//	std::cout << "Testing with animated model: 3D/animTVGuy.glb" << std::endl;

	//	// FIRST: Test coordinate system theory by comparing both loading systems
	//	std::cout << "\n=== ASSIMP vs TINY_OBJ_LOADER COMPARISON ===\n";
	//	std::cout << "Loading ship.obj with both systems...\n\n";

	//	// Load with TinyObjectLoader (working system)
	//	std::cout << "--- TINY_OBJ_LOADER OUTPUT ---\n";
	//	try {
	//		aveng::AvengModel::Builder builder;
	//		builder.loadModel("3D/animTVGuy.glb");

	//		std::cout << "TinyObjLoader: " << builder.vertices.size() << " vertices, "
	//			<< builder.indices.size() << " indices\n";

	//		// Print first 3 vertices for comparison
	//		for (size_t i = 0; i < std::min(size_t(3), builder.vertices.size()); ++i) {
	//			const auto& v = builder.vertices[i];
	//			std::cout << "TinyObj Vertex " << i << ":\n";
	//			std::cout << "  Position: (" << v.position.x << ", " << v.position.y << ", " << v.position.z << ")\n";
	//			std::cout << "  Normal:   (" << v.normal.x << ", " << v.normal.y << ", " << v.normal.z << ")\n";
	//		}

	//	}
	//	catch (const std::exception& e) {
	//		std::cout << "TinyObjLoader failed: " << e.what() << "\n";
	//	}

	//	std::cout << "\n--- ASSIMP OUTPUT (Coordinate conversion DISABLED) ---\n";
	//	// The Assimp output will come from the debugVertexData() function
	//	std::cout << "=== COMPARISON COMPLETE ===\n";

	//	// PERFORMANCE: Time the entire operation
	//	auto totalStartTime = std::chrono::high_resolution_clock::now();

	//	// Set up render data for tracking
	//	RenderData renderData{};

	//	// PERFORMANCE: Time the model loading
	//	auto loadStartTime = std::chrono::high_resolution_clock::now();

	//	// Load the animated model
	//	bool loadSuccess = animationManager.loadModel("3D/animTVGuy.glb", renderData);
	//	bool loadSuccess2 = animationManager.loadModel("3D/tv-man.obj", renderData);

	//	auto loadEndTime = std::chrono::high_resolution_clock::now();
	//	auto loadDuration = std::chrono::duration<float, std::chrono::milliseconds::period>(loadEndTime - loadStartTime).count();
	//	std::cout << "Model loading took: " << loadDuration << " ms" << std::endl;

	//	if (!loadSuccess || !loadSuccess2) {
	//		std::cout << "Failed to load .glb" << std::endl;
	//		return;
	//	}

	//	// Get the loaded model for inspection
	//	auto model = animationManager.getModel("3D/animTVGuy.glb");
	//	auto model2 = animationManager.getModel("3D/tv-man.obj");
	//	if (!model || !model2) {
	//		std::cout << "Could not retrieve loaded model" << std::endl;
	//		return;
	//	}

	//	// Display model information
	//	std::cout << "\nMODEL INFORMATION:" << std::endl;
	//	std::cout << "  Model filename: " << model->getModelFileName() << std::endl;
	//	std::cout << "  Triangle count: " << model->getTriangleCount() << std::endl;
	//	std::cout << "  Has animations: " << (model->hasAnimations() ? "YES" : "NO") << std::endl;

	//	// Display mesh information
	//	const auto& meshes = model->getModelMeshes();
	//	std::cout << "  Mesh count: " << meshes.size() << std::endl;
	//	for (size_t i = 0; i < meshes.size(); ++i) {
	//		std::cout << "    Mesh " << i << ": " << meshes[i].vertices.size() << " vertices, "
	//			<< meshes[i].indices.size() << " indices" << std::endl;
	//		std::cout << "      Has animation data: " << (meshes[i].hasAnimationData ? "YES" : "NO") << std::endl;
	//	}

	//	// Display bone information
	//	const auto& bones = model->getBoneList();
	//	std::cout << "  Bone count: " << bones.size() << std::endl;
	//	for (size_t i = 0; i < bones.size() && i < 10; ++i) { // Show first 10 bones
	//		std::cout << "    Bone " << i << ": " << bones[i]->getBoneName() << " (ID: " << bones[i]->getBoneId() << ")" << std::endl;
	//	}
	//	if (bones.size() > 10) {
	//		std::cout << "    ... and " << (bones.size() - 10) << " more bones" << std::endl;
	//	}

	//	// Display node hierarchy information
	//	const auto& nodes = model->getNodeList();
	//	std::cout << "  Node count: " << nodes.size() << std::endl;
	//	for (size_t i = 0; i < nodes.size() && i < 5; ++i) { // Show first 5 nodes
	//		std::cout << "    Node " << i << ": " << nodes[i]->getNodeName() << " (Parent: " << nodes[i]->getParentNodeName() << ")" << std::endl;
	//	}
	//	if (nodes.size() > 5) {
	//		std::cout << "    ... and " << (nodes.size() - 5) << " more nodes" << std::endl;
	//	}

	//	// Display animation information
	//	const auto& animations = model->getAnimClips();
	//	const auto& animations2 = model2->getAnimClips();
	//	std::cout << "  Animation count: " << animations.size() << std::endl;

	//	// Test instance creation
	//	std::cout << "\nTESTING INSTANCE CREATION:" << std::endl;

	//	auto instanceStartTime = std::chrono::high_resolution_clock::now();
	//	auto instance1 = animationManager.createInstance("3D/animTVGuy.glb", glm::vec3(-35, 0, 0));
	//	auto instanceEndTime = std::chrono::high_resolution_clock::now();

	//	auto instanceGlb = animationManager.createInstance("3D/tv-man.obj", glm::vec3(-30, 0, 0));

	//	auto instanceDuration = std::chrono::duration<float, std::chrono::milliseconds::period>(instanceEndTime - instanceStartTime).count();
	//	std::cout << "Instance creation took: " << instanceDuration << " ms" << std::endl;

	//	if (instance1 && instanceGlb) {
	//		std::cout << "Successfully created 1 animated cube instance!" << std::endl;

	//		// Test animation switching if animations are available
	//		if (!animations2.empty()) {
	//			std::cout << "\nTESTING ANIMATION CONTROL:" << std::endl;
	//			if (animations2.size() > 0) {
	//				instanceGlb->setAnimationByIndex(0);
	//				std::cout << "  Instance 1: Set to animation 0 (\"" << animations2[0]->getClipName() << "\")" << std::endl;
	//			}
	//		}
	//	}
	//	else {
	//		std::cout << "Failed to create instances" << std::endl;
	//	}

	//	// Display final debug information
	//	animationManager.resetRenderDataAnimationTotals(renderData);
	//	std::cout << "\nFINAL DEBUG STATS:" << std::endl;
	//	std::cout << "  Loaded models: " << renderData.rdLoadedModels << std::endl;
	//	std::cout << "  Animated models: " << renderData.rdAnimatedModels << std::endl;
	//	std::cout << "  Total bones: " << renderData.rdTotalBones << std::endl;
	//	std::cout << "  Total nodes: " << renderData.rdTotalNodes << std::endl;
	//	std::cout << "  Total animation clips: " << renderData.rdTotalAnimationClips << std::endl;
	//	std::cout << "  Active instances: " << renderData.rdActiveInstances << std::endl;

	//	// PERFORMANCE: Calculate total time
	//	auto totalEndTime = std::chrono::high_resolution_clock::now();
	//	auto totalDuration = std::chrono::duration<float, std::chrono::milliseconds::period>(totalEndTime - totalStartTime).count();

	//	std::cout << "\n PERFORMANCE SUMMARY:" << std::endl;
	//	std::cout << "  Initial animation system setup: " << totalDuration << " ms" << std::endl;
	//	std::cout << "  Note: DEBUG - Testing ANIMATED CUBE with REFERENCE NODE-BASED algorithm" << std::endl;

	//	std::cout << "\nAnimation system test completed!" << std::endl;
	//	std::cout << "=== END ANIMATION TEST ===\n" << std::endl;
	//}

	//void Renderer::updateAnimationSystem(float frameTime)
	//{
	//	// PERFORMANCE: Time animation updates (called every frame)
	//	static int frameCounter = 0;
	//	static float totalAnimTime = 0.0f;
	//	static float totalRendererTime = 0.0f;

	//	auto animStartTime = std::chrono::high_resolution_clock::now();

	//	// Update animation time
	//	animationManager.updateAnimations(frameTime);

	//	auto animEndTime = std::chrono::high_resolution_clock::now();
	//	auto animDuration = std::chrono::duration<float, std::chrono::milliseconds::period>(animEndTime - animStartTime).count();

	//	auto rendererStartTime = std::chrono::high_resolution_clock::now();

	//	// Get instances and pass to renderer with frame time
	//	const auto& instances = animationManager.getInstances();
	//	if (!instances.empty()) {
	//		updateAnimationData(instances, frameTime);
	//	}

	//	auto rendererEndTime = std::chrono::high_resolution_clock::now();
	//	auto rendererDuration = std::chrono::duration<float, std::chrono::milliseconds::period>(rendererEndTime - rendererStartTime).count();

	//	// Accumulate timing data
	//	totalAnimTime += animDuration;
	//	totalRendererTime += rendererDuration;
	//	frameCounter++;

	//	// Report every 120 frames (~2 seconds at 60fps)
	//	if (frameCounter >= 120) {
	//		float avgAnimTime = totalAnimTime / frameCounter;
	//		float avgRendererTime = totalRendererTime / frameCounter;
	//		std::cout << "DEBUG CUBE GEOMETRY PERFORMANCE (120 frames avg):" << std::endl;
	//		std::cout << "  Animation updates: " << avgAnimTime << " ms/frame" << std::endl;
	//		std::cout << "  Renderer updates: " << avgRendererTime << " ms/frame" << std::endl;
	//		std::cout << "  Total per frame: " << (avgAnimTime + avgRendererTime) << " ms/frame" << std::endl;

	//		// Reset counters
	//		frameCounter = 0;
	//		totalAnimTime = 0.0f;
	//		totalRendererTime = 0.0f;
	//	}
	//}

	void Renderer::renderEditor() {
		auto commandBuffer = getCurrentCommandBuffer();
		editor.render(commandBuffer);
	}

} // NS