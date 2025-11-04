#include "Renderer.h"
#include <iostream>
#include <stdexcept>
#include <cassert>
#include <array>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

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

		recreateSwapChain();

		createCommandBuffers();

		// Initialize PointLightSystem now that descriptor layouts are created
		initializePointLightSystem();

		editor.init(window, aveng_swapchain.get());

		sceneLoader.load(default_scene_file, engineDevice);

		const auto& sceneTextures = sceneLoader.getSceneTextures();
		std::cout << "Scene has " << sceneTextures.size() << " textures defined" << std::endl;

		// Initialize ImageSystem with scene textures
		initializeImageSystem(sceneTextures);

		// Initialize our descriptor sets, map buffers to device memory.
		setupDescriptors();

		// Create pipelines now that descriptor layouts are ready
		createPipelines();

	}

	Renderer::~Renderer()
	{
		std::cout << "Destroying Renderer..." << std::endl;
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

		// Resize our vector of command buffers to match the max number of images the swapchain will allow in 
		renderData.rdCommandBuffersGraphics.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdCommandBuffersCompute.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		// Note: This accepts a vector of buffers
		CommandBuffer::init(engineDevice, renderData.rdCommandBuffersGraphics);
		CommandBuffer::init(engineDevice, renderData.rdCommandBuffersCompute);

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

		// This might be better off happening AFTER a certain block of the draw() method
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

		// Define buffer vec's that are managed by the Renderer
		mPerspectiveViewMatrixUBOBuffers = std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		mShaderModelRootMatrixBuffers = std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		mNodeTransformBuffers = std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		mTrsMatrixBuffers = std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		mShaderBoneMatrixBuffers = std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		mLightDataBuffers = std::vector<std::unique_ptr<AvengBuffer>>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		// Note: Textures are stored in the imageSystem's imageViews

		// Define descriptor set vec's
		renderData.rdAvengDescriptorSets = std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);
		renderData.rdAvengAnimationDescriptorSets = std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);
		renderData.rdAvengComputeTransformDescriptorSets = std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);
		renderData.rdAvengComputeMatrixMultDescriptorSets = std::vector<VkDescriptorSet>(2, VK_NULL_HANDLE);

		// Create Buffers using VMA
		// VMA_MEMORY_USAGE_AUTO: Let VMA choose optimal memory type
		// VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT: CPU will write to this buffer frequently  
		// VMA_ALLOCATION_CREATE_MAPPED_BIT: Keep buffer persistently mapped for performance

		size_t bufferSize = 1024; // TODO?

		// Renderer::draw
		for (int i = 0; i < mPerspectiveViewMatrixUBOBuffers.size(); i++) {
			mPerspectiveViewMatrixUBOBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				bufferSize, 1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mPerspectiveViewMatrixUBOBuffers[i]->map();
		}

		// Renderer::draw
		for (int i = 0; i < mNodeTransformBuffers.size(); i++) {
			mNodeTransformBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(NodeTransformData), 1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mNodeTransformBuffers[i]->map();
		}

		// Renderer::draw
		for (int i = 0; i < mShaderModelRootMatrixBuffers.size(); i++) {
			mShaderModelRootMatrixBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				bufferSize, 1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mShaderModelRootMatrixBuffers[i]->map();
		}

		for (int i = 0; i < mTrsMatrixBuffers.size(); i++) {
			mTrsMatrixBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				bufferSize, 1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mTrsMatrixBuffers[i]->map();
		}

		for (int i = 0; i < mShaderBoneMatrixBuffers.size(); i++) {
			mShaderBoneMatrixBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				bufferSize, 1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
				VMA_MEMORY_USAGE_AUTO,
				1, // minOffsetAlignment
				VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
			mShaderBoneMatrixBuffers[i]->map();
		}

		// Renderer::draw but could move to the light system
		for (int i = 0; i < mLightDataBuffers.size(); i++) {
			mLightDataBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(LightsUbo), 1,
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
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
			.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, currentTextureCount) // Image Views
			.build();

		/* non-animated shader */
		renderData.rdAvengBasicDescriptorLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 1) // Perspective/View Matrix UBO
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

		//// Initialize animation rendering system only if it exists
		//if (animationSystem) {
		//	animationSystem->initializeDescriptors(*descriptorPool, SwapChain::MAX_FRAMES_IN_FLIGHT);
		//}

		updateDescriptorSets();
		updateComputeDescriptorSets();
		updateLightingDescriptorSets();
	
	}

	void Renderer::initializePointLightSystem()
	{
		if (!renderData.rdAvengBasicLightingDescriptorLayout) {
			throw std::runtime_error("Descriptor set layouts must be created before initializing PointLightSystem (call setupDescriptors first)");
		}

		std::cout << "Initializing PointLightSystem" << std::endl;

		// Initialize point light system using existing descriptor set layouts
		pointLightSystem.initialize(
			getSwapChainRenderPass(),
			renderData.rdAvengBasicDescriptorLayout->getDescriptorSetLayout(),
			renderData.rdAvengBasicLightingDescriptorLayout->getDescriptorSetLayout()
		);

		std::cout << "PointLightSystem initialized" << std::endl;
	}

	void Renderer::renderLights()
	{
		//pointLightSystem.render(
		//	//globalDescriptorSets[currentFrameIndex], 
		//	//lightsDescriptorSets[currentFrameIndex], 
		//	getCurrentCommandBufferGraphics(), 
		//	u_LightsData.numLights);
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
			renderData.rdAvengComputeTransformPipeline);
		vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeTransformaPipelineLayout, 0, 1, &renderData.rdAvengComputeTransformDescriptorSet, 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(renderData.rdComputeCommandBuffer, renderData.rdAvengComputeTransformaPipelineLayout,
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
			renderData.rdAvengComputeMatrixMultPipeline);

		VkDescriptorSet& modelDescriptorSet = model->getMatrixMultDescriptorSet();
		std::vector<VkDescriptorSet> computeSets = { renderData.rdAvengComputeMatrixMultDescriptorSet, modelDescriptorSet };
		vkCmdBindDescriptorSets(renderData.rdComputeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeMatrixMultPipelineLayout, 0, static_cast<uint32_t>(computeSets.size()), computeSets.data(), 0, 0);

		mUploadToUBOTimer.start();
		mComputeModelData.pkModelOffset = modelOffset;
		vkCmdPushConstants(renderData.rdComputeCommandBuffer, renderData.rdAvengComputeMatrixMultPipelineLayout,
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

		/* wait for both fences before getting the new framebuffer image */
		std::vector<VkFence> waitFences = { renderData.rdComputeFence, renderData.rdRenderFence };
		VkResult result = vkWaitForFences(engineDevice.device(),
			static_cast<uint32_t>(waitFences.size()), waitFences.data(), VK_TRUE, UINT64_MAX);
		if (result != VK_SUCCESS) {
			std::printf("%s error: waiting for fences failed (error: %i)\n", __FUNCTION__, result);
			return false;
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
		bufferResized = ShaderStorageBuffer::uploadSsboData(renderData, engineDevice, mShaderNodeTransformBuffer, mNodeTransFormData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		/* resize SSBO if needed */
		bufferResized |= ShaderStorageBuffer::checkForResize(renderData, engineDevice, mShaderTRSMatrixBuffer, boneMatrixBufferSize * sizeof(glm::mat4));
		bufferResized |= ShaderStorageBuffer::checkForResize(renderData, engineDevice, mShaderBoneMatrixBuffer, boneMatrixBufferSize * sizeof(glm::mat4));

		// Note: this occurs if ANY buffer has a new size
		if (bufferResized) {
			updateDescriptorSets();
			updateComputeDescriptorSets();
		}

		/* record compute commands */
		result = vkResetFences(engineDevice.device(), 1, &renderData.rdComputeFence);
		if (result != VK_SUCCESS) {
			std::printf("%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
			return false;
		}

		if (animatedModelLoaded) {
			if (!CommandBuffer::reset(renderData.rdComputeCommandBuffer, 0)) {
				::printf("%s error: failed to reset compute command buffer\n", __FUNCTION__);
				return false;
			}

			if (!CommandBuffer::beginSingleShot(renderData.rdComputeCommandBuffer)) {
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

			if (!CommandBuffer::end(renderData.rdComputeCommandBuffer)) {
				std::printf("%s error: failed to end compute command buffer\n", __FUNCTION__);
				return false;
			}

			/* submit compute commands */
			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			VkSubmitInfo computeSubmitInfo{};
			computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			computeSubmitInfo.commandBufferCount = 1;
			computeSubmitInfo.pCommandBuffers = &renderData.rdComputeCommandBuffer;
			computeSubmitInfo.signalSemaphoreCount = 1;
			computeSubmitInfo.pSignalSemaphores = &renderData.rdComputeSemaphore;
			computeSubmitInfo.waitSemaphoreCount = 1;
			computeSubmitInfo.pWaitSemaphores = &renderData.rdGraphicSemaphore;
			computeSubmitInfo.pWaitDstStageMask = &waitStage;

			result = vkQueueSubmit(renderData.rdComputeQueue, 1, &computeSubmitInfo, renderData.rdComputeFence);
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
			computeSubmitInfo.pSignalSemaphores = &renderData.rdComputeSemaphore;
			computeSubmitInfo.waitSemaphoreCount = 1;
			computeSubmitInfo.pWaitSemaphores = &renderData.rdGraphicSemaphore;
			computeSubmitInfo.pWaitDstStageMask = &waitStage;

			result = vkQueueSubmit(renderData.rdComputeQueue, 1, &computeSubmitInfo, renderData.rdComputeFence);
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
				return false;
			};
		}

		// handleMovementKeys();

		/* we need to update descriptors after the upload if buffer size changed */
		mUploadToUBOTimer.start();
		UniformBuffer::uploadData(renderData, engineDevice, mPerspectiveViewMatrixUBO, mMatrices);
		bufferResized = ShaderStorageBuffer::uploadSsboData(renderData, engineDevice, mShaderModelRootMatrixBuffer, mWorldPosMatrices);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

		if (bufferResized) {
			updateDescriptorSets();
		}

		/* start with graphics rendering */
		result = vkResetFences(engineDevice.device(), 1, &renderData.rdRenderFence);
		if (result != VK_SUCCESS) {
			std::printf("%s error:  fence reset failed (error: %i)\n", __FUNCTION__, result);
			return false;
		}

		if (!CommandBuffer::reset(renderData.rdCommandBuffer, 0)) {
			std::printf("%s error: failed to reset command buffer\n", __FUNCTION__);
			return false;
		}

		if (!CommandBuffer::beginSingleShot(renderData.rdCommandBuffer)) {
			std::printf("%s error: failed to begin command buffer\n", __FUNCTION__);
			return false;
		}

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

					vkCmdBindPipeline(renderData.rdCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdAvengSkinningPipeline);

					vkCmdBindDescriptorSets(renderData.rdCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
						renderData.rdAvengSkinningPipelineLayout, 1, 1, &renderData.rdAvengSkinningDescriptorSet, 0, nullptr);

					mUploadToUBOTimer.start();
					mModelData.pkModelStride = numberOfBones;
					mModelData.pkWorldPosOffset = worldPosOffset;
					mModelData.pkSkinMatOffset = skinMatOffset;
					vkCmdPushConstants(renderData.rdCommandBuffer, renderData.rdAvengSkinningPipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mModelData);
					renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

					model->drawInstancedV2(renderData, numberOfInstances);

					worldPosOffset += numberOfInstances;
					skinMatOffset += numberOfInstances * numberOfBones;
				}
				else {
					/* non-animated models */

					vkCmdBindPipeline(renderData.rdCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData.rdAvengPipeline);

					vkCmdBindDescriptorSets(renderData.rdCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
						renderData.rdAvengPipelineLayout, 1, 1, &renderData.rdAvengDescriptorSet, 0, nullptr);

					mUploadToUBOTimer.start();
					mModelData.pkModelStride = 0;
					mModelData.pkWorldPosOffset = worldPosOffset;
					mModelData.pkSkinMatOffset = 0;
					vkCmdPushConstants(renderData.rdCommandBuffer, renderData.rdAvengPipelineLayout,
						VK_SHADER_STAGE_VERTEX_BIT, 0, static_cast<uint32_t>(sizeof(VkPushConstants)), &mModelData);
					renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();

					model->drawInstancedV2(renderData, numberOfInstances);

					worldPosOffset += numberOfInstances;
				}
			}
		}

		// Is this necessary?
		if (!CommandBuffer::end(renderData.rdCommandBuffer)) {
			std::printf("%s error: failed to end command buffer\n", __FUNCTION__);
			return false;
		}

		return true;
	}

	void Renderer::updateDescriptorSets() {
		// Why auto - This can be removed to the ImageSystem class along with the descriptor writing
		// This descriptor would need to be rewritten at runtime to load new textures during run
		auto imageInfo = imageSystem->descriptorInfoForAllImages();

		// Write the descriptor sets that are ready to go
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			AvengDescriptorSetWriter(*renderData.rdAvengTextureDescriptorLayout, *descriptorPool)
				.writeImage(1, imageInfo.data(), imageInfo.size())
				.build(renderData.textureDescriptorSets[i]);

			auto perspectiveViewBufferInfo = mPerspectiveViewMatrixUBOBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0);
			auto modelRootBufferInfo = mShaderModelRootMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0);
			auto shaderBoneMatrixInfo = mShaderBoneMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0);

			// Basic Shader
			AvengDescriptorSetWriter(*renderData.rdAvengBasicDescriptorLayout, *descriptorPool)
				.writeBuffer(0, &perspectiveViewBufferInfo)
				.writeBuffer(1, &modelRootBufferInfo)
				.build(renderData.rdAvengDescriptorSets[i]);

			// Animation Shader
			AvengDescriptorSetWriter(*renderData.rdAvengAnimationDescriptorLayout, *descriptorPool)
				.writeBuffer(0, &perspectiveViewBufferInfo)
				.writeBuffer(1, &shaderBoneMatrixInfo)
				.writeBuffer(2, &modelRootBufferInfo)
				.build(renderData.rdAvengAnimationDescriptorSets[i]);

			// Reference if we decide to use a dynamic UBO
			// auto objBufferInfo = u_ObjBuffers[i]->descriptorInfo(calculateDynamicUBOStride(), 0);
		}
	}

	void Renderer::updateLightingDescriptorSets() {
		auto lightsBufferInfo = mLightDataBuffers[i]->descriptorInfo(sizeof(LightsUbo), 0);
		AvengDescriptorSetWriter(*renderData.rdAvengBasicLightingDescriptorLayout, *descriptorPool)
			.writeBuffer(0, &lightsBufferInfo)
			.build(renderData.basicLightingDescriptorSets[i]);
	}

	void Renderer::updateComputeDescriptorSets() {

		// Write the descriptor sets that are ready to go
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			auto nodeTransformInfo = mNodeTransformBuffers[i]->descriptorInfo(sizeof(NodeTransformData), 0);
			auto trsMatrixinfo = mTrsMatrixBuffers[i]->descriptorInfo(sizeof(VK_WHOLE_SIZE), 0);

			// Node Compute
			AvengDescriptorSetWriter(*renderData.rdAvengComputeTransformDescriptorLayout, *descriptorPool)
				.writeBuffer(0, &nodeTransformInfo)
				.writeBuffer(1, &trsMatrixinfo)
				.build(renderData.rdAvengDescriptorSets[i]);

			

		}

	}

	void Renderer::updateFrameData(const glm::mat4& projection, const glm::mat4& view)
	{
		mMatrices.projectionMatrix = projection;
		mMatrices.viewMatrix = view;
	}

} // NS