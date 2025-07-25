#include "Renderer.h"
#include <iostream>
#include <stdexcept>
#include <cassert>
#include <array>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#define LOG(a) std::cout<<a<<std::endl;
#define DESTROY_UNIFORM_BUFFERS 1

namespace aveng {

	Renderer::Renderer(AvengWindow& window, EngineDevice& device) 
		: aveng_window{ window }, engineDevice{ device }, pointLightSystem{ engineDevice }
	{
		recreateSwapChain();
		createCommandBuffers();
	}

	Renderer::~Renderer()
	{
		freeCommandBuffers();
		vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
	}

	/*
	* Note: Recreating the swapchain isn't sufficient if the image format changes
	* such as the window being moved to a different monitor. In that event it would
	* be necessary to recreate the renderpass.
	 */
	void Renderer::recreateSwapChain()
	{
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

	/*
	*/
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
	VkCommandBuffer Renderer::beginFrame() 
	{
		assert(!isFrameStarted && "Can't call beginFrame while already in progress.");

		auto result = aveng_swapchain->acquireNextImage(&currentImageIndex);

		// This error will occur after window resize
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			recreateSwapChain();
			return nullptr;
		}
		// This could potentially occur during window resize events
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			throw std::runtime_error("Failed to acquire swap chain image.");
		}

		isFrameStarted = true;

		auto commandBuffer = getCurrentCommandBuffer();

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

		if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
		{
			throw std::runtime_error("Command Buffer failed to begin recording.");
		}

		return commandBuffer;

	}

	// 
	void  Renderer::endFrame()
	{
		assert(isFrameStarted && "Can't call endFrame while frame is not in progress.");
		auto commandBuffer = getCurrentCommandBuffer();
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

	void Renderer::setupDescriptors(int numObjects)
	{
		if (!imageSystem) {
			throw std::runtime_error("ImageSystem must be initialized before setting up descriptors (call initializeImageSystem first)");
		}
		
		num_objects = numObjects;
		auto imageInfo = imageSystem->descriptorInfoForAllImages();

		// Create Descriptor Pools using dynamic texture count
		descriptorPool = AvengDescriptorPool::Builder(engineDevice)
			.setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * 4)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 2)
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * currentTextureCount)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, SwapChain::MAX_FRAMES_IN_FLIGHT)
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
				calculateDynamicUBOStride(), num_objects,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				calculateDynamicUBOStride(), // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			u_ObjBuffers[i]->map();
		}

		// Verify VMA allocation is working
		std::cout << "VMA Buffer Allocation Verification:" << std::endl;
		std::cout << "  Global Buffer [0] using VMA: " << (u_GlobalBuffers[0]->isUsingVMA() ? "YES" : "NO") << std::endl;
		std::cout << "  Lights Buffer [0] using VMA: " << (u_LightsBuffers[0]->isUsingVMA() ? "YES" : "NO") << std::endl;
		std::cout << "  Object Buffer [0] using VMA: " << (u_ObjBuffers[0]->isUsingVMA() ? "YES" : "NO") << std::endl;

		// Check memory budget after buffer creation
		engineDevice.printMemoryStats();
		if (engineDevice.isMemoryPressureHigh()) {
			std::cout << "WARNING: High memory pressure detected!" << std::endl;
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
		
		// Initialize instanced rendering system
		setupInstanceBuffers();
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

	void Renderer::updateFrameData(const GlobalUbo& globalData, const LightsUbo& lightsData)
	{
		// Update global uniform buffer
		u_GlobalBuffers[currentFrameIndex]->writeToBuffer(&globalData);
		u_GlobalBuffers[currentFrameIndex]->flush();

		// Update lights uniform buffer
		u_LightsBuffers[currentFrameIndex]->writeToBuffer(&lightsData);
		u_LightsBuffers[currentFrameIndex]->flush();
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

			// Push constants - now properly handled
			struct PushConstantData {
				glm::mat4 modelMatrix;
				glm::mat4 normalMatrix;
			} pushData{ modelMatrix, normalMatrix };

			vkCmdPushConstants(commandBuffer, pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0, sizeof(PushConstantData), &pushData);

			// Bind and draw model
			model->bind(commandBuffer);
			model->draw(commandBuffer);
		}
	}

	void Renderer::renderLights(int numLights)
	{
		pointLightSystem.render(globalDescriptorSets[currentFrameIndex], 
			lightsDescriptorSets[currentFrameIndex], getCurrentCommandBuffer(), numLights);
	}

	void Renderer::renderObjectsInstanced(const std::vector<std::tuple<ObjectUniformData, glm::mat4, glm::mat4, AvengModel*>>& objectData)
	{
		if (!instancedRenderingEnabled || objectData.empty()) {
			// Fallback to traditional rendering
			renderObjects(objectData);
			return;
		}

		std::cout << "=== Instanced Rendering ===" << std::endl;
		std::cout << "Processing " << objectData.size() << " objects for instancing" << std::endl;

		auto commandBuffer = getCurrentCommandBuffer();

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

			renderBatches[model]->instances.push_back(instanceData);
		}

		// Bind pipeline based on current render mode
		GFXPipeline* activePipeline = nullptr;
		
		if (pipelineManager) {
			activePipeline = pipelineManager->getPipeline(static_cast<int>(currentObjectMode));
			if (activePipeline) {
				activePipeline->bind(commandBuffer);
			}
		}
		
		if (!activePipeline) {
			std::cerr << "WARNING: No pipeline found for instanced mode " << static_cast<int>(currentObjectMode) << std::endl;
			renderObjects(objectData); // Fallback
			return;
		}

		// Bind global descriptor set (set 0)
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			0, 1, &globalDescriptorSets[currentFrameIndex], 0, nullptr);

		// Bind lights descriptor set (set 2)
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
			2, 1, &lightsDescriptorSets[currentFrameIndex], 0, nullptr);

		// Render each batch
		uint32_t totalInstancesRendered = 0;
		uint32_t batchesRendered = 0;
		
		for (auto& [model, batch] : renderBatches) {
			if (batch->instances.empty()) continue;

			uint32_t instanceCount = static_cast<uint32_t>(batch->instances.size());
			std::cout << "Rendering batch: " << instanceCount << " instances of model " << model << std::endl;

			// Update instance buffer for this batch
			updateInstanceBuffer(*batch);

			// For now, use push constants for the first instance (we'll optimize this later)
			if (!batch->instances.empty()) {
				struct PushConstantData {
					glm::mat4 modelMatrix;
					glm::mat4 normalMatrix;
				} pushData{ 
					batch->instances[0].modelMatrix,
					batch->instances[0].normalMatrix 
				};

				vkCmdPushConstants(commandBuffer, pipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0, sizeof(PushConstantData), &pushData);
			}

			// Bind object descriptor set (set 1) - use first instance's texture
			if (!batch->instances.empty()) {
				ObjectUniformData objUniform{ batch->instances[0].textureIndex };
				u_ObjBuffers[currentFrameIndex]->writeToIndex(&objUniform, 0);
				u_ObjBuffers[currentFrameIndex]->flushIndex(0);
				auto descriptorInfo = u_ObjBuffers[currentFrameIndex]->descriptorInfoForIndex(0);
				uint32_t dynamicOffset = static_cast<uint32_t>(descriptorInfo.offset);

				vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
					1, 1, &objectDescriptorSets[currentFrameIndex], 1, &dynamicOffset);
			}

			// Bind model vertex and index buffers
			model->bind(commandBuffer);

			// TODO: Bind instance buffer as additional vertex buffer
			// For now, render multiple instances using traditional draw calls
			// This gives us the batching benefit while we work on shader updates
			for (uint32_t i = 0; i < instanceCount; ++i) {
				// Update push constants for each instance
				struct PushConstantData {
					glm::mat4 modelMatrix;
					glm::mat4 normalMatrix;
				} pushData{ 
					batch->instances[i].modelMatrix,
					batch->instances[i].normalMatrix 
				};

				vkCmdPushConstants(commandBuffer, pipelineLayout,
					VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
					0, sizeof(PushConstantData), &pushData);

				// Draw single instance
				model->draw(commandBuffer);
			}

			totalInstancesRendered += instanceCount;
			batchesRendered++;
		}

		std::cout << "Instanced rendering complete: " << batchesRendered << " batches, " 
		          << totalInstancesRendered << " total instances" << std::endl;
		
		// Performance analysis
		float efficiency = (float)totalInstancesRendered / (float)batchesRendered;
		if (efficiency > 2.0f) {
			std::cout << "Instancing is BENEFICIAL! Avg " << efficiency << " instances per batch" << std::endl;
		} else if (efficiency > 1.0f) {
			std::cout << "Instancing provides MINOR benefit. Avg " << efficiency << " instances per batch" << std::endl;
		} else {
			std::cout << "Instancing provides NO benefit (efficiency: " << efficiency << ")" << std::endl;
			std::cout << "Auto-switching to traditional rendering for better performance" << std::endl;
			
			// Automatically disable instancing for inefficient scenes
			static int inefficientFrameCount = 0;
			inefficientFrameCount++;
			if (inefficientFrameCount >= 5) {
				std::cout << "Automatically disabling instanced rendering - scene not suitable" << std::endl;
				instancedRenderingEnabled = false;
			}
		}
	}

	void Renderer::setupInstanceBuffers()
	{
		std::cout << "Setting up instance buffers for instanced rendering" << std::endl;
		// Instance buffers will be created on-demand in updateInstanceBuffer()
		// This avoids pre-allocating buffers for models that might not be used
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
		batch.instanceBuffer->writeToBuffer(batch.instances.data(), 
			sizeof(InstanceData) * instanceCount);
		batch.instanceBuffer->flush();
	}

	void Renderer::createPipelineLayout()
	{
		if (!globalDescriptorSetLayout || !objDescriptorSetLayout || !lightsDescriptorSetLayout) {
			throw std::runtime_error("Descriptor set layouts must be created before pipeline layout (call setupDescriptors first)");
		}

		// Create descriptor set layouts array using stored layouts
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts(3);
		descriptorSetLayouts[0] = globalDescriptorSetLayout->getDescriptorSetLayout();
		descriptorSetLayouts[1] = objDescriptorSetLayout->getDescriptorSetLayout();
		descriptorSetLayouts[2] = lightsDescriptorSetLayout->getDescriptorSetLayout();

		// Push constant range
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(glm::mat4) * 2; // modelMatrix + normalMatrix

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 3;
		pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		if (vkCreatePipelineLayout(engineDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
			throw std::runtime_error("failed to create pipeline layout!");
		}
	}

	// DEPRECATED: Legacy pipeline creation - use PipelineConfigManager instead
	void Renderer::createPipeline()
	{
		assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

		PipelineConfig pipelineConfig{};
		GFXPipeline::defaultPipelineConfig(pipelineConfig);
		pipelineConfig.renderPass = getSwapChainRenderPass();
		pipelineConfig.pipelineLayout = pipelineLayout;

		// Setup specialization constants for dynamic texture array size
		VkSpecializationMapEntry specEntry{};
		specEntry.constantID = 0;  // Matches constant_id = 0 in shader
		specEntry.offset = 0;
		specEntry.size = sizeof(uint32_t);

		VkSpecializationInfo specInfo{};
		specInfo.mapEntryCount = 1;
		specInfo.pMapEntries = &specEntry;
		specInfo.dataSize = sizeof(uint32_t);
		specInfo.pData = &currentTextureCount;

		pipelineConfig.fragmentSpecializationInfo = &specInfo;

		std::cout << "Creating pipeline with texture array size: " << currentTextureCount << std::endl;

		gfxPipeline = std::make_unique<GFXPipeline>(
			engineDevice,
			"shaders/simple_shader.vert.spv",
			"shaders/simple_shader.frag.spv",
			pipelineConfig
		);

		gfxPipeline2 = std::make_unique<GFXPipeline>(
			engineDevice,
			"shaders/simple_shader2.vert.spv",
			"shaders/simple_shader2.frag.spv",
			pipelineConfig
		);
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
			
			createPostProcessPipelines();  // Post-processing pipelines (placeholder)
			pipelineCreated = true;
			std::cout << "Pipeline initialization complete" << std::endl;
		}
	}

	// DEPRECATED: Hardcoded pipeline array - use PipelineConfigManager instead
	void Renderer::createObjectPipelines()
	{
		assert(pipelineLayout != nullptr && "Cannot create pipelines before pipeline layout");

		// Setup specialization constants for dynamic texture array size (shared by all pipelines)
		VkSpecializationMapEntry specEntry{};
		specEntry.constantID = 0;  // Matches constant_id = 0 in shader
		specEntry.offset = 0;
		specEntry.size = sizeof(uint32_t);

		VkSpecializationInfo specInfo{};
		specInfo.mapEntryCount = 1;
		specInfo.pMapEntries = &specEntry;
		specInfo.dataSize = sizeof(uint32_t);
		specInfo.pData = &currentTextureCount;

		// Resize pipeline vector to match enum size
		objectPipelines.resize(static_cast<size_t>(ObjectRenderMode::DISTORTED) + 1);

		std::cout << "Creating object rendering pipelines..." << std::endl;

		// STANDARD pipeline
		{
			PipelineConfig standardConfig{};
			GFXPipeline::defaultPipelineConfig(standardConfig);
			standardConfig.renderPass = getSwapChainRenderPass();
			standardConfig.pipelineLayout = pipelineLayout;
			standardConfig.fragmentSpecializationInfo = &specInfo;

			objectPipelines[static_cast<size_t>(ObjectRenderMode::STANDARD)] = std::make_unique<GFXPipeline>(
				engineDevice,
				"shaders/simple_shader.vert.spv",
				"shaders/simple_shader.frag.spv",
				standardConfig
			);
		}

		// WIREFRAME pipeline
		{
			PipelineConfig wireframeConfig{};
			GFXPipeline::defaultPipelineConfig(wireframeConfig);
			wireframeConfig.renderPass = getSwapChainRenderPass();
			wireframeConfig.pipelineLayout = pipelineLayout;
			wireframeConfig.fragmentSpecializationInfo = &specInfo;
			wireframeConfig.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;

			objectPipelines[static_cast<size_t>(ObjectRenderMode::WIREFRAME)] = std::make_unique<GFXPipeline>(
				engineDevice,
				"shaders/simple_shader.vert.spv",
				"shaders/simple_shader.frag.spv",
				wireframeConfig
			);
		}

		// DISTORTED pipeline (temporarily use same shaders as standard - we'll add effects later)
		{
			PipelineConfig distortedConfig{};
			GFXPipeline::defaultPipelineConfig(distortedConfig);
			distortedConfig.renderPass = getSwapChainRenderPass();
			distortedConfig.pipelineLayout = pipelineLayout;
			distortedConfig.fragmentSpecializationInfo = &specInfo;

			objectPipelines[static_cast<size_t>(ObjectRenderMode::DISTORTED)] = std::make_unique<GFXPipeline>(
				engineDevice,
				"shaders/simple_shader.vert.spv",  // Use working shaders for now
				"shaders/simple_shader.frag.spv",
				distortedConfig
			);
		}

		std::cout << "Created " << objectPipelines.size() << " object pipelines" << std::endl;
	}

	void Renderer::createPostProcessPipelines()
	{
		std::cout << "Creating post-processing pipelines..." << std::endl;

		// For now, create placeholder - full post-processing setup would require:
		// 1. Offscreen render targets
		// 2. Full-screen quad rendering
		// 3. Screen-space effect shaders

		// Resize pipeline vector to match enum size  
		postProcessPipelines.resize(static_cast<size_t>(PostProcessMode::CHROMATIC_ABERRATION) + 1);

		// TODO: Implement full post-processing system
		// This would include:
		// - Creating offscreen framebuffers
		// - Post-process specific pipeline layouts
		// - Full-screen quad geometry generation
		// - Screen-space effect shaders (toxic_cloud.frag, night_vision.frag, etc.)

		std::cout << "Post-processing system placeholder created (full implementation needed)" << std::endl;
	}

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

} // NS