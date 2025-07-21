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
		: aveng_window{ window }, engineDevice{ device }, imageSystem{ engineDevice }, pointLightSystem{ engineDevice }
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

	void Renderer::setupDescriptors(int numObjects)
	{
		num_objects = numObjects;
		auto imageInfo = imageSystem.descriptorInfoForAllImages();

		// Create Descriptor Pools
		descriptorPool = AvengDescriptorPool::Builder(engineDevice)
			.setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * 4)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 2)
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * imageInfo.size())
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

		// Create Buffers
		for (int i = 0; i < u_GlobalBuffers.size(); i++) {
			u_GlobalBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(GlobalUbo), 1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			u_GlobalBuffers[i]->map();
		}

		for (int i = 0; i < u_LightsBuffers.size(); i++) {
			u_LightsBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(LightsUbo), 1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			u_LightsBuffers[i]->map();
		}

		for (int i = 0; i < u_ObjBuffers.size(); i++) {
			u_ObjBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				calculateDynamicUBOStride(), num_objects,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
				calculateDynamicUBOStride());
			u_ObjBuffers[i]->map();
		}

		// Create Descriptor Set Layouts
		std::unique_ptr<AvengDescriptorSetLayout> globalDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, imageInfo.size())
			.build();

		std::unique_ptr<AvengDescriptorSetLayout> objDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.build();

		std::unique_ptr<AvengDescriptorSetLayout> lightsDescriptorSetLayout =
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

		// Create pipelines
		createPipelineLayout();
		createPipeline();

		// Initialize point light system
		pointLightSystem.initialize(
			getSwapChainRenderPass(),
			globalDescriptorSetLayout->getDescriptorSetLayout(),
			lightsDescriptorSetLayout->getDescriptorSetLayout()
		);
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

		// Bind pipeline
		gfxPipeline->bind(commandBuffer);

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

	void Renderer::createPipelineLayout()
	{
		// Create descriptor set layouts array
		std::vector<VkDescriptorSetLayout> descriptorSetLayouts(3);
		
		// This is a simplified version - in practice, we'd need to store the layouts
		// For now, we'll create them inline (not ideal, but functional)
		auto imageInfo = imageSystem.descriptorInfoForAllImages();
		
		auto globalDescriptorSetLayout = AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, imageInfo.size())
			.build();
			
		auto objDescriptorSetLayout = AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.build();
			
		auto lightsDescriptorSetLayout = AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.build();

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

	void Renderer::createPipeline()
	{
		assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

		PipelineConfig pipelineConfig{};
		GFXPipeline::defaultPipelineConfig(pipelineConfig);
		pipelineConfig.renderPass = getSwapChainRenderPass();
		pipelineConfig.pipelineLayout = pipelineLayout;

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

	size_t Renderer::calculateDynamicUBOStride() const
	{
		size_t objectSize = sizeof(ObjectUniformData);
		size_t minAlignment = engineDevice.properties.limits.minUniformBufferOffsetAlignment;
		return ((objectSize + minAlignment - 1) / minAlignment) * minAlignment;
	}

} // NS