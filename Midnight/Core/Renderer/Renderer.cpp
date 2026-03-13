#include <cstring>
//#define STB_IMAGE_IMPLEMENTATION
//#include "stb/stb_image.h"
#include "Renderer.h"
#include "CoreVK/Resources/platform.h"
#include "avpch.h"
#include "Core/Renderer/FramePacketBuilder.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/SkinningPipeline.h"
#include "CoreVK/ComputePipeline.h"
#include "Core/Texture.h"
#include "CoreVK/Resources/gpu_resources.h"
// #include "CoreVK/LinePipeline.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "CoreVK/AvengUniformBuffer.h"
#include "CoreVK/PipelineLayout.h"
#include "CoreVK/SyncObjects.h"
#include "Core/Modeling/AssimpInstance.h"
#include "Core/aveng_window.h"
#include "Runtime/Facade/SceneFacade.h" // IRenderSceneView, currently
#include "Game/Camera/CameraManager.h"
#include "Core/Renderer/ModelLibrary.h"

#define LOG(a) std::cout<<a<<std::endl;
#define DESTROY_UNIFORM_BUFFERS 1	// Unused as far as I can tell

namespace aveng {
	// Narrow interface into modelDb_ specifically for a model's animation metadata
	//const IModelAnimQuery& Renderer::animQuery() const { return modelDb_; }
	/* <END> Query Services */

	Renderer::Renderer(
		EngineDevice& engineDevice, 
		AvengWindow& window,
		VkRenderData& _renderData, 
		CameraManager& _cameraManager,
		const IModelQuery& mq,
		IModelAnimQuery& aq)
		:   engineDevice	{ engineDevice }, 
			aveng_window	{ window }, 
			renderData		{ _renderData }, 
			cameraManager	{ _cameraManager },
			modelQuery_		{ mq },
			animQuery_		{ aq }
	{

		buffer_trash.clear();
		//mWorldPosMatrices.clear();
		//mNodeTransFormData.clear();

		// Buffered by frames-in-flight (single, double or triple)
		mPerspectiveViewMatrixUBOBuffers = std::vector<VkUniformBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mPointLightUBOBuffers			 = std::vector<VkUniformBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdBoneMetaBuffers	 = std::vector<VkUniformBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mModelMatrixBuffers				 = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mNodeTransformBuffers			 = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderTrsMatrixBuffers			 = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mShaderBoneMatrixBuffers		 = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		mMaterialBuffers				 = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdInstanceMaterialBuffers		= std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdShaderBoneMatrixOffsetBuffers  = std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);
		renderData.rdBoneParentNodeIndexBuffers		= std::vector<VkShaderStorageBufferData>(SwapChain::MAX_FRAMES_IN_FLIGHT);

		// Define descriptor set vec's
		// renderData.rdAvengDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		// renderData.rdAvengAnimationDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		// renderData.rdAvengComputeTransformDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		// renderData.rdAvengComputeMatrixMultDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		renderData.rdAvengBasicTerrainDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		renderData.rdAvengBindlessDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		renderData.rdAvengComputeBasicTerrainDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
		// renderData.basicLightingDescriptorSets = std::vector<VkDescriptorSet>(SwapChain::MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

		recreateSwapChain();

		createCommandBuffers();

		if (!createMatrixUBO()) {
			throw std::runtime_error("Failed to create UBOs");
		}

		if (!createSSBOs()) {
			throw std::runtime_error("Failed to create SSBOs");
		}

		if (!createLightsUBO()) {
			throw std::runtime_error("Failed to create Lighting UBOs");
		}

		//
		createDefaultSamplers();

		/* Init Bindless Resources - TODO cleanup */
		createBindlessDescriptors();

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

		// Renderer's only callback registration - See InstanceManager for others
		//mModelInstanceCallbacks.miInstanceCenterCallbackFunction = [this](const InstanceHandle& handle) { centerInstance(handle); };

		/* signal graphics semaphores before doing anything else to be able to run compute submit */
		/* Only needed when using separate graphics and compute queues */
		if (!engineDevice.sameGraphicsComputeQueue()) {
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
		} else {
			std::cout << "[Renderer] Same queue detected - skipping graphics/compute semaphore sync" << std::endl;
		}

		std::printf("Vulkan renderer initialized!\n");

	}

	bool Renderer::createDefaultSamplers() {
		auto createSampler = [&](VkFilter filter,
			VkSamplerAddressMode addressMode,
			VkSampler* outSampler) -> bool {
			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = filter;
			samplerInfo.minFilter = filter;
			samplerInfo.mipmapMode = (filter == VK_FILTER_LINEAR)
				? VK_SAMPLER_MIPMAP_MODE_LINEAR
				: VK_SAMPLER_MIPMAP_MODE_NEAREST;

			samplerInfo.addressModeU = addressMode;
			samplerInfo.addressModeV = addressMode;
			samplerInfo.addressModeW = addressMode;

			samplerInfo.mipLodBias = 0.0f;
			samplerInfo.anisotropyEnable = VK_FALSE;     // turn on later if desired
			samplerInfo.maxAnisotropy = 1.0f;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

			samplerInfo.minLod = 0.0f;
			samplerInfo.maxLod = VK_LOD_CLAMP_NONE;

			samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;

			VkResult result = vkCreateSampler(
				engineDevice.device(),
				&samplerInfo,
				nullptr,
				outSampler
			);

			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: vkCreateSampler failed (%d)\n", __FUNCTION__, result);
				return false;
			}

			return true;
		};

		if (!createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			&renderData.default_samplers.linearRepeat)) {
			return false;
		}

		if (!createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			&renderData.default_samplers.linearClamp)) {
			return false;
		}

		if (!createSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			&renderData.default_samplers.nearestRepeat)) {
			return false;
		}

		return true;
	}

	void Renderer::initialize() {
		initializePointLights();


		addLight(
			glm::vec3(20.0f, 0.0f, 20.0f),
			glm::vec3(0.f, 0.0f, 1.0f),
			15.f,
			1.0f
		);

		addLight(
			glm::vec3(-20.0f, 0.0f, 20.0f),
			glm::vec3(0.95f, .88f, 1.0f),
			15.f,
			1.0f
		);

		addLight(
			glm::vec3(20.0f, 0.0f, -20.0f),
			glm::vec3(0.f, 1.0f, 0.0f),    
			15.f,
			1.0f
		);

		addLight(
			glm::vec3(-20.0f, 0.0f, -20.0f),
			glm::vec3(0.9f, 0.08f, 0.02f),  
			15.f,
			1.0f                 
		);

		addLight(
			glm::vec3(0.0f, -30.0f, 0.0f),
			glm::vec3(0.7f, 0.98f, 0.98f),
			8.0f,
			4.0f
		);

	}

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

		// Reset per-image fence tracking after swapchain recreation
		renderData.rdImagesInFlight.clear();
		renderData.rdImagesInFlight.resize(aveng_swapchain->imageCount(), VK_NULL_HANDLE);

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
		// Re-enabled compute fence to ensure compute command buffer is safe to reuse
		std::vector<VkFence> waitFences = { renderData.rdComputeFence.at(currentFrameIndex), renderData.rdRenderFence.at(currentFrameIndex)};
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

		// Wait for any previous frame that was using this swapchain image to complete
		// This handles the case where frame-in-flight index != swapchain image index
		if (renderData.rdImagesInFlight[currentImageIndex] != VK_NULL_HANDLE) {
			result = vkWaitForFences(
				engineDevice.device(),
				1,
				&renderData.rdImagesInFlight[currentImageIndex],
				VK_TRUE,
				UINT64_MAX
			);
			if (result != VK_SUCCESS) {
				std::printf("%s error: waiting for image fence failed (error: %i)\n", __FUNCTION__, result);
				throw std::runtime_error("waiting for image fence failed");
			}
		}
		// Mark that this swapchain image is now being used by the current frame's fence
		renderData.rdImagesInFlight[currentImageIndex] = renderData.rdRenderFence.at(currentFrameIndex);

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
			"[Renderer] Can't begin render pass on command buffer from a different frame");

		// Clear Color for now
		glm::vec3 rgb = glm::vec3(0.001f, 0.002f, 0.009f); // Cool, dark midnight blue

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
	 
	void Renderer::initializePointLights()
	{

		glm::vec3 midnightBlue = glm::vec3(0.97f, 0.89f, 0.9f);
		mPointLightData.ambientLightColor = glm::vec4(midnightBlue, 0.01f);
		pointLightSystem.initialize(getSwapChainRenderPass(), 1, false);

	}

	/* Render the lighting billboards */
	void Renderer::renderLights()
	{
		
		if (mPointLightData.numLights <= 0) {
			return; // Nothing to render
		}
		assert(renderData.rdCommandBuffersGraphics.at(currentFrameIndex) == getCurrentCommandBufferGraphics()
			&& "Point Light system is using the wrong command buffer");

		// This might not be necessary
		vkCmdBindPipeline(renderData.rdCommandBuffersGraphics.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_GRAPHICS, pointLightSystem.getPipeline());

		// Use instanced rendering: 6 vertices per light, numLights instances
		vkCmdDraw(renderData.rdCommandBuffersGraphics.at(currentFrameIndex), 6, mPointLightData.numLights, 0, 0);
	}

	void Renderer::addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius)
	{
		if (mPointLightData.numLights >= 200) {
			std::cout << "Warning: Maximum number of lights (" << 200 << ") reached. Cannot add more lights." << std::endl;
			return;
		}

		std::cout << "Adding Light..." << std::endl;

		mPointLightData.positions[mPointLightData.numLights] = glm::vec4(position, radius);
		mPointLightData.colors[mPointLightData.numLights] = glm::vec4(color, intensity);
		mPointLightData.numLights++;

		// Update the UBO in the most inefficent way possible... for now!
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			UniformBuffer::uploadPersistentData(engineDevice, mPointLightUBOBuffers[i], mPointLightData);
		}

	}

	void Renderer::clearLights()
	{
		mPointLightData.numLights = 0;
		// Zero out the light arrays for clean state
		memset(mPointLightData.positions, 0, sizeof(mPointLightData.positions));
		memset(mPointLightData.colors, 0, sizeof(mPointLightData.colors));
	}

	bool Renderer::createPipelineLayouts() {

		/* Bindless Pipeline Layout - Combined resources to support all shader stages in 1 pipeline */
		std::vector<VkDescriptorSetLayout> layouts = { renderData.rdBindlessDescriptorLayout };

		std::vector<VkPushConstantRange> pushConstants = { 
			// Note: these stage flags must match stageFlags arg of vkCmdPushConstants
			{ VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkPushConstants) },
			{ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VkComputePushConstants) }
			// Note that if stages overlapped on usage across different pc's then we couldn't start both offsets at 0
			// E.g.
			/*layout(push_constant) uniform Constants {
				layout(offset = 16) uint modelOffset;
				layout(offset = 20) uint skinMetaIndex;
			} pc;*/
		};

		if (!PipelineLayout::init(engineDevice, renderData.rdAvengBindlessPipelineLayout, layouts, pushConstants)) {
			std::printf("%s error: could not init Assimp pipeline layout\n", __FUNCTION__);
			return false;
		}

		return true;
	}

	bool Renderer::createPipelines() {

		VkRenderPass renderPass = aveng_swapchain->getRenderPass();

		// Basic Pipeline
		std::string vertexShaderFile = "shaders/assimp.vert.spv";
		std::string fragmentShaderFile = "shaders/assimp.frag.spv";
		if (!SkinningPipeline::init(engineDevice, renderData.rdAvengBindlessPipelineLayout,
			renderData.rdAvengPipeline, renderPass, 1, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp shader pipeline\n", __FUNCTION__);
			return false;
		}

		// Animation Pipeline
		vertexShaderFile = "shaders/assimp_skinning.vert.spv";
		fragmentShaderFile = "shaders/assimp_skinning.frag.spv";
		if (!SkinningPipeline::init(engineDevice,  renderData.rdAvengBindlessPipelineLayout,
			renderData.rdAvengAnimationPipeline, renderPass, 1, vertexShaderFile, fragmentShaderFile)) {
			std::printf("%s error: could not init Assimp Skinning shader pipeline\n", __FUNCTION__);
			return false;
		}

		// Animation Compute Input Pipeline
		std::string computeShaderFile = "shaders/assimp_instance_transform.comp.spv";
		if (!ComputePipeline::init(engineDevice, renderData.rdAvengBindlessPipelineLayout,
			renderData.rdAvengComputeTransformPipeline, computeShaderFile)) {
			std::printf("%s error: could not init Assimp Transform compute shader pipeline\n", __FUNCTION__);
			return false;
		}

		// Animation Compute Output Pipeline - WHY IS THIS A 2ND PIPELINE YOU FOOL
		computeShaderFile = "shaders/assimp_instance_matrix_mult.comp.spv";
		if (!ComputePipeline::init(engineDevice, renderData.rdAvengBindlessPipelineLayout,
			renderData.rdAvengComputeMatrixMultPipeline, computeShaderFile)) {
			std::printf("%s error: could not init Assimp Matrix Mult compute shader pipeline\n", __FUNCTION__);
			return false;
		}

		// For future reference, we'll be adding a line drawing pipeline during the game's runtime too, not just the editor.
		//vertexShaderFile = "shader/line.vert.spv";
		//fragmentShaderFile = "shader/line.frag.spv";
		//if (!LinePipeline::init(engineDevice, renderPass, renderData.rdLinePipelineLayout, renderData.rdLinePipeline,
		//	vertexShaderFile, fragmentShaderFile)) {
		//	Logger::log(1, "%s error: could not init Assimp line drawing shader pipeline\n", __FUNCTION__);
		//	return false;
		//}

		return true;
	}

	// Nice to have, if I should utilize dynamic UBOs (again!)
	size_t Renderer::calculateDynamicUBOStride() const
	{
		//size_t objectSize = sizeof(ObjectUniformData);
		//size_t minAlignment = engineDevice.properties.limits.minUniformBufferOffsetAlignment;
		//return ((objectSize + minAlignment - 1) / minAlignment) * minAlignment;
		return 0;
	}

	void Renderer::runComputeShaders(const AvengModel* model, int numInstances, uint32_t modelOffset, uint32_t skinMetaOffset, uint32_t numberOfBones) {

		/*
		* Potential for optimization:
		* Use Combine these two shaders into 1 uber shader. They're linearly dependent so separating 
		* these animation compute stages might not be 100% necessary.
		* This only works as long as skinning reads from only identical invocations. I need to learn more about how invocations/warps work.
		* 
		* Or, VK_EXT_shader_object and redesign the whole lot (might not be relevant to this particular renderer)
		* 
		* Moderninzing:
		* TODO: Since we support Vulkan 1.3+, refer vkCmdPipelineBarrier2 with VkBufferMemoryBarrier2 and explicit stage/access masks.
		*/

		/*
		* Good thing to note here about synchronization:
		* `vkCmdPipelineBarrier` creates a dependency for commands before/after it in the same command buffer.
		* Our draws are in a different command buffer than compute cmd's, so the barrier below can't directly 
		* form a dependency with graphics commands.
		* This is why we use a semaphore to signal 
		*/

		mComputeTimer.start();
		mComputeModelData.pkModelOffset = modelOffset; // Identical to the VkPushConstants::pkInstanceBaseIndex - for parallel instance data (base)
		mComputeModelData.skinMetaIndex = skinMetaOffset;

		VkDescriptorSet computeSets[1] = { renderData.rdAvengBindlessDescriptorSets[currentFrameIndex] };

		// Bind once on VK_PIPELINE_BIND_POINT_COMPUTE
		vkCmdBindDescriptorSets(renderData.rdCommandBuffersCompute.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengBindlessPipelineLayout, 0, ArraySize(computeSets), computeSets, 0, 0);

		// VkCommandBuffer computeCommandBuffer = getCurrentCommandBufferCompute();
		uint32_t groupsY = (numInstances + 31) / 32;

		/* node transformation */
		vkCmdBindPipeline(renderData.rdCommandBuffersCompute.at(currentFrameIndex), VK_PIPELINE_BIND_POINT_COMPUTE,
			renderData.rdAvengComputeTransformPipeline);

		vkCmdPushConstants(
			renderData.rdCommandBuffersCompute.at(currentFrameIndex),
			renderData.rdAvengBindlessPipelineLayout, // Unified bindless layout
			VK_SHADER_STAGE_COMPUTE_BIT,
			0,
			sizeof(VkComputePushConstants),
			&mComputeModelData
		);

		vkCmdDispatch(renderData.rdCommandBuffersCompute.at(currentFrameIndex), numberOfBones, groupsY, 1);

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

		vkCmdDispatch (renderData.rdCommandBuffersCompute.at(currentFrameIndex), numberOfBones, groupsY, 1);

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
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1, // TODO - Inspect the dstStageMask usage
			&boneMatrixBufferBarrier, 0, nullptr);

		renderData.rdComputeTime += mComputeTimer.stop();

	}

	/**
	* By the time this method is called:
	* - Fences have all been waited on and signaled.
	* - The latest swapchain image has been received
	* - isFrameStarted = true
	* - Primary Graphics Command buffer is reset and in a recording state.
	* - Primary graphics renderpass has begun
	* 
	*/
	int Renderer::update(const FramePacket& pkt, const ModelLibrary& modelLib, float deltaTime) {

		/* no update on zero diff. This caused an issue */
		if (deltaTime == 0.0f && !firstFrame) {
			isFrameStarted = false;
			return -1;
		}

		if (!isFrameStarted) {
			std::printf("beginFrame failed/skipped (swapchain recreation), skipping frame\n");
			return -1;  // Skip this frame gracefully
		}

		/*
		* TIL: mNodeTransFormData is updated via instance->updateAnimation()
		*	   That is also when & where we update mWorldPosMatrices.
		*	   TRS and BoneMat buffers are updated by the compute shaders so you won't see uploadSsboData here on their behalf
		*/
		// Timer
		mUploadToSSBO1Timer.start();
		bool bufferResized = false;

		// Compute stage 1 input
		// TODO - This upload only needs to occur if there are Animated Models!
		bufferResized = ShaderStorageBuffer::uploadPersistentSsboData(engineDevice, mNodeTransformBuffers.at(currentFrameIndex), pkt.nodeTransformData);
		// Upload every instance's current transform data (translation, scale, rotation)
		// If it resized, no data was uploaded
		if (bufferResized) {

			size_t newBufferSize = std::max(pkt.nodeTransformData.size() * (sizeof NodeTransformData), mNodeTransformBuffers.at(currentFrameIndex).bufferSize * 2);

			// Queue the old buffers for destruction within the next frame
			buffer_trash.push_back(
				PendingBufferDestroy{
					mNodeTransformBuffers.at(currentFrameIndex).buffer,
					mNodeTransformBuffers.at(currentFrameIndex).bufferAlloc,
				}
			);

			// Reinitialize - New Allocation
			ShaderStorageBuffer::init(engineDevice, mNodeTransformBuffers.at(currentFrameIndex), MapMode::Persistent, ResidentMode::CPU, newBufferSize);

			// Retry the upload - true == it resized again after we just tried to reallocate it. That would be bad
			if (ShaderStorageBuffer::uploadPersistentSsboData(engineDevice, mNodeTransformBuffers.at(currentFrameIndex), pkt.nodeTransformData))
			{
				std::printf("[1] Failed to accommodate resized buffer.\n");
				throw std::runtime_error("[1] Failed to accommodate resized buffer.");
			}

		}

		// Timer
		renderData.rdUploadSSBO1Time += mUploadToSSBO1Timer.stop();

		// Feels hacky
		boneMatrixBufferSize = pkt.nodeTransformData.size();

		/* resize SSBO if needed - Both of these are written by the compute shaders so we just make sure they're the right size (hence only using checkForResize) */

		// Compute stage 2 read (stage 1 out)
		bool trsResized = ShaderStorageBuffer::checkForResize(engineDevice, mShaderTrsMatrixBuffers.at(currentFrameIndex), boneMatrixBufferSize * sizeof(glm::mat4));
		// Compute stage 2 write (vertex in)
		bool boneResized = ShaderStorageBuffer::checkForResize(engineDevice, mShaderBoneMatrixBuffers.at(currentFrameIndex), boneMatrixBufferSize * sizeof(glm::mat4));

		if (trsResized || boneResized) {

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
				MapMode::GpuOnly,
				ResidentMode::GPU,
				newBufferSize // New buffer size - these buffers are very similar
			);

			// Reinitialize TrsMat buffers
			ShaderStorageBuffer::init(
				engineDevice,
				mShaderTrsMatrixBuffers.at(currentFrameIndex),
				MapMode::GpuOnly,
				ResidentMode::GPU,
				newBufferSize // New buffer size - these buffers are very similar
			);

			std::cout << "Compute Buffers TRSMat | BoneMat Buffers Resized" << std::endl;

		}

		// Timer
		/* we need to update descriptors after the upload if buffer size changed - These 2 ubo's won't resize under normal circumstances */
		mUploadToUBOTimer.start();
		UniformBuffer::uploadPersistentData(engineDevice, mPerspectiveViewMatrixUBOBuffers.at(currentFrameIndex), mMatrices); // View/Proj Matrices
		UniformBuffer::uploadPersistentData(engineDevice, mPointLightUBOBuffers.at(currentFrameIndex), mPointLightData);
		renderData.rdUploadToUBOTime += mUploadToUBOTimer.stop();	

		// Timer
		mUploadToSSBO2Timer.start();

		// Each instance's model matrix - TODO - Audit persistence
		bool modelMatsResized = ShaderStorageBuffer::uploadSsboData(engineDevice, mModelMatrixBuffers.at(currentFrameIndex), pkt.modelMats);

		// Upload the model mat's
		if (modelMatsResized) {
			
			size_t newBufferSize = std::max(pkt.modelMats.size() * sizeof glm::mat4, mModelMatrixBuffers.at(currentFrameIndex).bufferSize * 2);

			buffer_trash.push_back(
				PendingBufferDestroy{
					mModelMatrixBuffers.at(currentFrameIndex).buffer,
					mModelMatrixBuffers.at(currentFrameIndex).bufferAlloc,
				}
			);

			// Reinitialize TrsMat buffers
			ShaderStorageBuffer::init(
				engineDevice,
				mModelMatrixBuffers.at(currentFrameIndex),
				MapMode::Persistent,
				ResidentMode::CPU,
				newBufferSize // New buffer size
			);

			if (ShaderStorageBuffer::uploadSsboData(engineDevice, mModelMatrixBuffers.at(currentFrameIndex), pkt.modelMats)) {
				std::printf("[2] Failed to accommodate resized buffer.\n");
				throw std::runtime_error("[2] Failed to accommodate resized buffer.");
			}

			std::cout << "Model Mats SSBO Resized..." << std::endl;
			
		}

		bufferResized |= trsResized;
		bufferResized |= boneResized;
		bufferResized |= modelMatsResized;

		if (bufferResized)
		{
			std::cout << "Updating Descriptor Sets" << std::endl;
			updateBindlessDescriptorSets(currentFrameIndex);
		}

		// Timer
		renderData.rdUploadSSBO2Time = mUploadToSSBO2Timer.stop();

	}

	int Renderer::dispatchCompute(const IModelLibrary& modelLib, const FramePacket& pkt) {

		/* record compute commands */
		result = vkResetFences(engineDevice.device(), 1, &renderData.rdComputeFence.at(currentFrameIndex));
		mComputeTimer.start();
		if (result != VK_SUCCESS) {
			std::printf("%s error: compute fence reset failed (error: %i)\n", __FUNCTION__, result);
			return WTF_BOOM;
		}

		if (pkt.animatedInstanceCount > 0) {

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

			// Dispatch compute shaders for all animated batches in the FramePacket - begin at the end of static batch offset
			for (uint32_t i = pkt.staticBatchCount; i < pkt.batches.size(); ++i) {
				const DrawBatch& b = pkt.batches[i];
				if (b.boneCount == 0 || b.instanceCount == 0) continue;

				const AvengModel* model = modelLib.pModel(b.modelId); /* TODO */
				if (!model) continue;
				//std::vector<glm::mat4> debugBoneMat(model->getMatOffBuffer(currentFrameIndex).bufferSize, glm::mat4(1.0f));
				//ShaderStorageBuffer::uploadSsboData(engineDevice, model->getMatOffBuffer(currentFrameIndex), debugBoneMat);
				runComputeShaders(model, b.alignedInstanceCount, b.boneBaseOffset, b.skinMetaOffset, b.boneCount);
			}

			// End command recording for Compute Queue
			if (!engineDevice.endCommandBuffer(renderData.rdCommandBuffersCompute[currentFrameIndex])) {
				std::printf("%s error: failed to end compute command buffer\n", __FUNCTION__);
				return WTF_BOOM;
			}

			/* submit compute commands */
			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			VkSubmitInfo computeSubmitInfo{};
			computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			computeSubmitInfo.commandBufferCount = 1;
			computeSubmitInfo.pCommandBuffers = &renderData.rdCommandBuffersCompute[currentFrameIndex];

			// When graphics and compute use the same queue, skip semaphore sync
			// Submission order on the same queue guarantees execution order
			if (!engineDevice.sameGraphicsComputeQueue()) {
				computeSubmitInfo.signalSemaphoreCount = 1;
				computeSubmitInfo.pSignalSemaphores = &renderData.rdComputeSemaphore[currentFrameIndex];
				computeSubmitInfo.waitSemaphoreCount = 1;
				computeSubmitInfo.pWaitSemaphores = &renderData.rdGraphicSemaphore[currentFrameIndex];
				computeSubmitInfo.pWaitDstStageMask = &waitStage;
			}

			result = vkQueueSubmit(engineDevice.computeQueue(), 1, &computeSubmitInfo, renderData.rdComputeFence[currentFrameIndex]);
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
				return WTF_BOOM;
			};

		}
		else {
			/* do an empty submit if we don't have animated models to satisfy fence and semaphore */
			VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

			VkSubmitInfo computeSubmitInfo{};
			computeSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

			// When graphics and compute use the same queue, skip semaphore sync
			if (!engineDevice.sameGraphicsComputeQueue()) {
				computeSubmitInfo.signalSemaphoreCount = 1;
				computeSubmitInfo.pSignalSemaphores = &renderData.rdComputeSemaphore[currentFrameIndex];
				computeSubmitInfo.waitSemaphoreCount = 1;
				computeSubmitInfo.pWaitSemaphores = &renderData.rdGraphicSemaphore[currentFrameIndex];
				computeSubmitInfo.pWaitDstStageMask = &waitStage;
			}

			// Compute submission and fence signaling
			result = vkQueueSubmit(engineDevice.computeQueue(), 1, &computeSubmitInfo, renderData.rdComputeFence[currentFrameIndex]);
			if (result != VK_SUCCESS) {
				std::printf("%s error: failed to submit compute command buffer (%i)\n", __FUNCTION__, result);
				return WTF_BOOM;
			};
		}

		renderData.rdComputeTime = mComputeTimer.stop();
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

	void Renderer::updateLights() {
		// Renderer lighting
		renderLights();
	}

	bool Renderer::drawModels(
		const FramePacket& pkt,
		const IModelLibrary& modelLib,
		VkCommandBuffer commandBuffer,
		VkPipeline basicPipeline,
		VkPipeline animationPipeline,
		VkPipelineLayout basicLayout,
		VkPipelineLayout animationLayout,
		VkDescriptorSet basicDescriptorSet,
		VkDescriptorSet animationDescriptorSet,
		int frameIndex)
	{

		// ========== STATIC PASS ==========
		// Bind static pipeline once, then draw all static batches
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, basicPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			basicLayout, 1, 1, &basicDescriptorSet, 0, nullptr);

		for (uint32_t i = 0; i < pkt.staticBatchCount; ++i) {
			const DrawBatch& b = pkt.batches[i];
			if (b.instanceCount == 0) continue;

			// Pointer here? Really?
			const AvengModel* model = modelLib.pModel(b.modelId); /* modelRegistry_.get(b.modelId) */
			if (!model) continue;

			mModelPushConst.pkInstanceBaseIndex = b.drawListOffset; // first instance index in global buffers
			mModelPushConst.pkModelBoneStride = 0;	// Unused for static batches
			mModelPushConst.pkSkinMatOffset = 0;	// Unused for static batches
			mModelPushConst.pkPickId = renderData.selectedPickId;
			vkCmdPushConstants(commandBuffer, basicLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkPushConstants), &mModelPushConst);

			model->drawInstancedV2(commandBuffer, basicLayout, b.instanceCount, frameIndex);
		}

		// ========== ANIMATED PASS ==========
		// Bind animated pipeline once, then draw all animated batches
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, animationPipeline);
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			animationLayout, 1, 1, &animationDescriptorSet, 0, nullptr);

		for (uint32_t i = pkt.staticBatchCount; i < pkt.batches.size(); ++i) {
			const DrawBatch& b = pkt.batches[i];
			if (b.instanceCount == 0) continue;

			const AvengModel* model = modelLib.pModel(b.modelId); /* modelRegistry_.get(b.modelId) */
			if (!model) continue;

			mModelPushConst.pkInstanceBaseIndex = b.drawListOffset;
			mModelPushConst.pkModelBoneStride = b.boneCount;
			mModelPushConst.pkSkinMatOffset = b.boneBaseOffset;
			mModelPushConst.pkPickId = renderData.selectedPickId;
			vkCmdPushConstants(commandBuffer, animationLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VkPushConstants), &mModelPushConst);

			model->drawInstancedV2(commandBuffer, animationLayout, b.instanceCount, frameIndex);
		}

		return true;
	}
	
	bool Renderer::drawModelsBindless(
		const FramePacket& pkt,
		const IModelLibrary& modelLib,
		VkCommandBuffer commandBuffer,
		VkPipeline basicPipeline,
		VkPipeline animationPipeline,
		int frameIndex)
	{
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, basicPipeline);

		VkDescriptorSet renderSets[1] = { renderData.rdAvengBindlessDescriptorSets[currentFrameIndex] };
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			renderData.rdAvengBindlessPipelineLayout, 1, 1, renderSets, 0, nullptr);

		mModelPushConst.pkModelBoneStride = 0;
		mModelPushConst.pkSkinMatOffset = 0;
		mModelPushConst.pkPickId = renderData.selectedPickId;

		// ========== Static Draws ==========
		for (uint32_t i = 0; i < pkt.staticBatchCount; ++i) {
			const DrawBatch& b = pkt.batches[i];
			if (b.instanceCount == 0){ continue; }

			// Pointer here? Really?
			const AvengModel* model = modelLib.pModel(b.modelId); /* modelRegistry_.get(b.modelId) */
			if (!model){ continue; }

			mModelPushConst.pkInstanceBaseIndex = b.drawListOffset;
			vkCmdPushConstants(
				commandBuffer,
				renderData.rdAvengBindlessPipelineLayout, // or your unified layout, whichever created basicPipeline
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0,
				sizeof(VkPushConstants),
				&mModelPushConst
			);
			model->drawInstancedV3(commandBuffer, renderData.rdAvengBindlessPipelineLayout, b.instanceCount, frameIndex);
		}

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, animationPipeline);

		// ========== Animated Draws ==========
		for (uint32_t i = pkt.staticBatchCount; i < pkt.batches.size(); ++i) {
			const DrawBatch& b = pkt.batches[i];
			if (b.instanceCount == 0){ continue; }

			const AvengModel* model = modelLib.pModel(b.modelId); /* modelRegistry_.get(b.modelId) */
			if (!model){ continue; }

			mModelPushConst.pkInstanceBaseIndex = b.drawListOffset;
			mModelPushConst.pkModelBoneStride = b.boneCount;
			mModelPushConst.pkSkinMatOffset = b.boneBaseOffset;
			mModelPushConst.pkPickId = renderData.selectedPickId;
			vkCmdPushConstants(
				commandBuffer,
				renderData.rdAvengBindlessPipelineLayout, // or your unified layout, whichever created basicPipeline
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0,
				sizeof(VkPushConstants),
				&mModelPushConst
			);

			// We can get rid of this indirection if we implement vertex pulling, 
			// or simply store each model's verts/indices in an external registry.
			// Also needs the model's VkMesh data. Getting there!
			model->drawInstancedV3(commandBuffer, renderData.rdAvengBindlessPipelineLayout, b.instanceCount, frameIndex);
		}

		return true;
	}

	// Timers in this class
	void Renderer::reset_timers()
	{
		renderData.rdDrawTime = mDrawTimer.stop();
		mDrawTimer.start();

		// animatedModelLoaded = false;
		renderData.rdMatricesSize = 0;
		renderData.rdUploadToUBOTime = 0.0f;
		renderData.rdUploadSSBO1Time = 0.0f;
		renderData.rdUploadSSBO2Time = 0.0f;
		renderData.rdMatrixGenerateTime = 0.0f;
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

		// Initialize per-image fence tracking (one per swapchain image, not per frame-in-flight)
		renderData.rdImagesInFlight.resize(aveng_swapchain->imageCount(), VK_NULL_HANDLE);

		if (!SyncObjects::init(engineDevice, renderData, SwapChain::MAX_FRAMES_IN_FLIGHT)) {
			std::printf("%s error: could not create sync objects\n", __FUNCTION__);
			return false;
		}
		return true;
	}

	void Renderer::cleanup() {

		destroyTrash();

		freeCommandBuffers();

		///* Moved to ModelLibrary */
		//for (const auto& model : mModelInstanceData.miModelList) {
		//	model->cleanup(engineDevice, renderData);
		//}

		//for (const auto& model : mModelInstanceData.miPendingDeleteAvengModels) {
		//	model->cleanup(engineDevice, renderData);
		//}

		SyncObjects::cleanup(engineDevice, renderData, SwapChain::MAX_FRAMES_IN_FLIGHT);

		SkinningPipeline::cleanup(engineDevice, renderData.rdAvengPipeline);
		SkinningPipeline::cleanup(engineDevice, renderData.rdAvengAnimationPipeline);
		ComputePipeline::cleanup(engineDevice, renderData.rdAvengComputeTransformPipeline);
		ComputePipeline::cleanup(engineDevice, renderData.rdAvengComputeMatrixMultPipeline);

		PipelineLayout::cleanup(engineDevice, renderData.rdAvengPipelineLayout);
		PipelineLayout::cleanup(engineDevice, renderData.rdAvengAnimationPipelineLayout);
		PipelineLayout::cleanup(engineDevice, renderData.rdAvengComputeTransformPipelineLayout);
		PipelineLayout::cleanup(engineDevice, renderData.rdAvengComputeMatrixMultPipelineLayout);
		PipelineLayout::cleanup(engineDevice, renderData.rdAvengComputeBasicTerrainPipelineLayout);

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			UniformBuffer::cleanup(engineDevice, mPerspectiveViewMatrixUBOBuffers[i]);
			UniformBuffer::cleanup(engineDevice, mPointLightUBOBuffers[i]);
			UniformBuffer::cleanup(engineDevice, renderData.rdBoneMetaBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, renderData.rdShaderBoneMatrixOffsetBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, renderData.rdBoneParentNodeIndexBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mShaderTrsMatrixBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mNodeTransformBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mModelMatrixBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mShaderBoneMatrixBuffers[i]);
			ShaderStorageBuffer::cleanup(engineDevice, mMaterialBuffers[i]);

			// TODO Material Buffers

			// vkFreeDescriptorSets(engineDevice.device(), renderData.avengDescriptorPool, 1, &renderData.rdAvengDescriptorSets[i]);
			// vkFreeDescriptorSets(engineDevice.device(), renderData.avengDescriptorPool, 1, &renderData.rdAvengAnimationDescriptorSets[i]);
			// vkFreeDescriptorSets(engineDevice.device(), renderData.avengDescriptorPool, 1, &renderData.rdAvengComputeTransformDescriptorSets[i]);
			// vkFreeDescriptorSets(engineDevice.device(), renderData.avengDescriptorPool, 1, &renderData.rdAvengComputeMatrixMultDescriptorSets[i]);
			vkFreeDescriptorSets(engineDevice.device(), renderData.avengDescriptorPool, 1, &renderData.rdAvengComputeBasicTerrainDescriptorSets[i]);

			vkFreeDescriptorSets(engineDevice.device(), renderData.avengBindlessDescriptorPool, 1, &renderData.rdAvengBindlessDescriptorSets[i]);

		}

		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengAnimationDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengTextureDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengComputeTransformDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengComputeMatrixMultDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengComputeMatrixMultPerModelDescriptorLayout, nullptr);
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdAvengComputeBasicTerrainDescriptorLayout, nullptr);

		//
		vkDestroyDescriptorSetLayout(engineDevice.device(), renderData.rdBindlessDescriptorLayout, nullptr);

		//
		vkDestroyDescriptorPool(engineDevice.device(), renderData.avengDescriptorPool, nullptr);

		// Bindless
		vkDestroyDescriptorPool(engineDevice.device(), renderData.avengBindlessDescriptorPool, nullptr);

		std::printf("%s: Vulkan renderer destroyed\n", __FUNCTION__);
	}

	bool Renderer::createMatrixUBO() {

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			if (!UniformBuffer::init(engineDevice, mPerspectiveViewMatrixUBOBuffers[i], sizeof(VkUploadMatrices), MapMode::Persistent)) {
				Logger::log(1, "%s error: could not create matrix uniform buffers\n", __FUNCTION__);
				return false;
			}

			// New - The SkinMeta UBO (bindless binding 11)
			if (!UniformBuffer::init(engineDevice, renderData.rdBoneMetaBuffers[i], 5120, MapMode::OnDemand)) {
				Logger::log(1, "%s error: could not create matrix uniform buffers\n", __FUNCTION__);
				return false;
			}
		}

		// Populate the shared view for the editor - for when it needs to update descriptor sets on behalf of its shaders
		renderData.matrixBuffersView.viewProjUBOs = {
			mPerspectiveViewMatrixUBOBuffers.data(),
			mPerspectiveViewMatrixUBOBuffers.size()
		};

		return true;
	}

	bool Renderer::createLightsUBO() {

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			std::cout << "Creating persistent Lights UBO" << std::endl;
			if (!UniformBuffer::init(engineDevice, mPointLightUBOBuffers[i], sizeof(PointLightData), MapMode::Persistent)) {
				Logger::log(1, "%s error: could not create lighting uniform buffers\n", __FUNCTION__);
				return false;
			}
		}

		// Populate the shared view for the editor - for when it needs to update its descriptor sets
		renderData.pointLightBufferView.viewPointLightUBOs = {
			mPointLightUBOBuffers.data(),
			mPointLightUBOBuffers.size()
		};
			
		return true;
	}

	bool Renderer::createBindlessDescriptors()
	{

		VkDescriptorPoolSize b_poolSizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * MAX_BINDLESS_TEXTURES },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,			 SwapChain::MAX_FRAMES_IN_FLIGHT * MAX_BINDLESS_TEXEL_BUFFERS },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,		 SwapChain::MAX_FRAMES_IN_FLIGHT * MAX_BINDLESS_BUFFERS },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,		 SwapChain::MAX_FRAMES_IN_FLIGHT * MAX_BINDLESS_BUFFERS }
		};

		VkDescriptorPoolCreateInfo b_poolInfo{};
		b_poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		b_poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		b_poolInfo.poolSizeCount = static_cast<uint32_t>(ArraySize(b_poolSizes));
		b_poolInfo.pPoolSizes = b_poolSizes;
		b_poolInfo.maxSets = SwapChain::MAX_FRAMES_IN_FLIGHT;

		VkResult result = vkCreateDescriptorPool(engineDevice.device(), &b_poolInfo, nullptr, &renderData.avengBindlessDescriptorPool);
		if (result != VK_SUCCESS) {
			Logger::log(1, "%s error: could not init descriptor pool (error: %i)\n", __FUNCTION__, result);
			return false;
		}

		createBindlessDescriptorLayouts();
		createBindlessDescriptorSets();

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
			updateBindlessDescriptorSets(i);
			updatePGStorageImageDescriptor(i);
		}

	}

	bool Renderer::createBindlessDescriptorLayouts() {
	
		VkDescriptorSetLayoutBinding bindless_bindings[12];

		// Texture array
		VkDescriptorSetLayoutBinding& sampler_binding = bindless_bindings[0];
		sampler_binding.binding = BINDLESS_TEXTURE_BINDING_0;
		sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		sampler_binding.descriptorCount = MAX_BINDLESS_TEXTURES;
		sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		sampler_binding.pImmutableSamplers = nullptr;

		// Texel image
		VkDescriptorSetLayoutBinding& storage_image_binding = bindless_bindings[1];
		storage_image_binding.binding = 1;
		storage_image_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		storage_image_binding.descriptorCount = 1; // MAX_BINDLESS_TEXEL_BUFFERS; TBD!
		storage_image_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;
		storage_image_binding.pImmutableSamplers = nullptr;

		// NodeTransformBuffers Compute stage 1 - input
		VkDescriptorSetLayoutBinding& ssbo1_binding = bindless_bindings[2];
		ssbo1_binding.binding = 2;
		ssbo1_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		ssbo1_binding.descriptorCount = 1;
		ssbo1_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		ssbo1_binding.pImmutableSamplers = nullptr;

		// mShaderTrsMatrixBuffers - Compute Stage 2 input
		VkDescriptorSetLayoutBinding& ssbo2_binding = bindless_bindings[3];
		ssbo2_binding.binding = 3;
		ssbo2_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		ssbo2_binding.descriptorCount = 1;
		ssbo2_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		ssbo2_binding.pImmutableSamplers = nullptr;

		// mShaderBoneMatrixBuffers - Compute Stage 2 output
		VkDescriptorSetLayoutBinding& ssbo3_binding = bindless_bindings[4];
		ssbo3_binding.binding = 4;
		ssbo3_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		ssbo3_binding.descriptorCount = 1;
		ssbo3_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT;
		ssbo3_binding.pImmutableSamplers = nullptr;

		// Model Matrices - aligned
		VkDescriptorSetLayoutBinding& ubo1_binding = bindless_bindings[5];
		ubo1_binding.binding = 5;
		ubo1_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		ubo1_binding.descriptorCount = 1;
		ubo1_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		ubo1_binding.pImmutableSamplers = nullptr;

		// Light Data
		VkDescriptorSetLayoutBinding& ubo2_binding = bindless_bindings[6];
		ubo2_binding.binding = 6;
		ubo2_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		ubo2_binding.descriptorCount = 1;
		ubo2_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		ubo2_binding.pImmutableSamplers = nullptr;

		// Materials
		VkDescriptorSetLayoutBinding& ssbo4_binding = bindless_bindings[7];
		ssbo4_binding.binding = 7;
		ssbo4_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		ssbo4_binding.descriptorCount = 1;
		ssbo4_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		ssbo4_binding.pImmutableSamplers = nullptr;

		// View, Proj mats
		VkDescriptorSetLayoutBinding& viewProj_binding = bindless_bindings[8];
		viewProj_binding.binding = 8;
		viewProj_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		viewProj_binding.descriptorCount = 1;
		viewProj_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		viewProj_binding.pImmutableSamplers = nullptr;

		// BoneOffsets
		VkDescriptorSetLayoutBinding& boneOffset_binding = bindless_bindings[9];
		boneOffset_binding.binding = 9;
		boneOffset_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		boneOffset_binding.descriptorCount = 1;
		boneOffset_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		boneOffset_binding.pImmutableSamplers = nullptr;

		// ParentMatrixIndices
		VkDescriptorSetLayoutBinding& boneParentIndices_binding = bindless_bindings[10];
		boneParentIndices_binding.binding = 10;
		boneParentIndices_binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		boneParentIndices_binding.descriptorCount = 1;
		boneParentIndices_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		boneParentIndices_binding.pImmutableSamplers = nullptr;

		// ModelSkinMeta
		VkDescriptorSetLayoutBinding& boneMeta_binding = bindless_bindings[11];
		boneMeta_binding.binding = 11;
		boneMeta_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		boneMeta_binding.descriptorCount = 1;
		boneMeta_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
		boneMeta_binding.pImmutableSamplers = nullptr;

		// TODO - Query device support for these bits!! (Enabled in EngineDevice `indexingFeatures`)
		// This means that bindings that are not dynamically used (same index across invocations) 
		// do not need to contain valid descriptors.
		VkDescriptorBindingFlags bindless_flags[12]{};
		bindless_flags[0] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

		VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
		flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
		flagsInfo.bindingCount = ArraySize(bindless_bindings);
		flagsInfo.pBindingFlags = bindless_flags;
		
		VkDescriptorSetLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		layoutInfo.bindingCount = ArraySize(bindless_bindings);
		layoutInfo.pBindings = bindless_bindings;
		layoutInfo.pNext = &flagsInfo;
		layoutInfo.flags = 0; // keep 0 until we begin using UPDATE_AFTER_BIND (although from another layout, most likely)

		/*
		 *	Binding Flags are part of Vulkan Core as of 1.2. They allow the use of:
		 *	VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT — allows updating descriptors after the set is bound.
		 *	VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT — allows some descriptors in an array to be unbound.
		 *	VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT — allows variable-sized descriptor arrays.
		 *	VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT_EXT — allows updating descriptors that are not currently used.
		 */

		result = vkCreateDescriptorSetLayout(engineDevice.device(), &layoutInfo, nullptr, 
									&renderData.rdBindlessDescriptorLayout);
		if (result != VK_SUCCESS) {
			Logger::log(1, "%s error: could not create bindless descriptor set layouts\n", __FUNCTION__, result);
			return false;
		}
		return true;

	}

	bool Renderer::createBindlessDescriptorSets() {

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
			allocInfo.descriptorPool = renderData.avengBindlessDescriptorPool;
			allocInfo.descriptorSetCount = 1;
			allocInfo.pSetLayouts = &renderData.rdBindlessDescriptorLayout;

			result = vkAllocateDescriptorSets(engineDevice.device(), &allocInfo, &renderData.rdAvengBindlessDescriptorSets[i]);
			if (result != VK_SUCCESS) {
				Logger::log(1, "%s error: could not allocate bindless descriptor sets\n", __FUNCTION__, result);
				return false;
			}

		}

		return true;
	}

	// Call this upon loading a texture. Good luck
	void Renderer::updateTextureArrayDescriptorSet(TextureHandle handle, VkImageView view, VkSampler sampler, int frameIndex) {

		// Binding 0 - Texture sampler array
		VkDescriptorImageInfo texArrayInfo{};
		texArrayInfo.sampler = sampler;
		texArrayInfo.imageView = view;
		texArrayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet writeTextureArray{};
		writeTextureArray.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeTextureArray.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeTextureArray.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		writeTextureArray.dstBinding = 0;
		writeTextureArray.descriptorCount = 1;
		writeTextureArray.pImageInfo = &texArrayInfo;
		writeTextureArray.dstArrayElement = static_cast<uint32_t>(handle);

		vkUpdateDescriptorSets(engineDevice.device(), 1, &writeTextureArray, 0, nullptr);

	}

	void Renderer::updatePGStorageImageDescriptor(int frameIndex) {

		VkDescriptorImageInfo storageImageInfo{};
		storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		storageImageInfo.imageView = renderData.pgStorageImage[frameIndex].view;
		storageImageInfo.sampler = VK_NULL_HANDLE; // storage images do not use samplers

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		write.dstBinding = 1;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		write.descriptorCount = 1;
		write.pImageInfo = &storageImageInfo;

		vkUpdateDescriptorSets(engineDevice.device(), 1, &write, 0, nullptr);
	}

	void Renderer::updateBindlessDescriptorSets(int frameIndex) {

		// Binding 11 - compute - ModelSkinMeta (BoneMetaBuffer)
		VkDescriptorBufferInfo boneMetaInfo{};
		boneMetaInfo.buffer = renderData.rdBoneMetaBuffers[frameIndex].buffer;
		boneMetaInfo.offset = 0;
		boneMetaInfo.range = VK_WHOLE_SIZE;

		// Binding 10 - compute - ParentMatrixIndices
		VkDescriptorBufferInfo parentNodeInfo{};
		parentNodeInfo.buffer = renderData.rdBoneParentNodeIndexBuffers[frameIndex].buffer;
		parentNodeInfo.offset = 0;
		parentNodeInfo.range = VK_WHOLE_SIZE;

		// Binding 9 - compute - BoneOffsets
		VkDescriptorBufferInfo boneOffsetInfo{};
		boneOffsetInfo.buffer = renderData.rdShaderBoneMatrixOffsetBuffers[frameIndex].buffer;
		boneOffsetInfo.offset = 0;
		boneOffsetInfo.range = VK_WHOLE_SIZE;

		// Binding 8
		VkDescriptorBufferInfo viewProjInfo{};
		viewProjInfo.buffer = mPerspectiveViewMatrixUBOBuffers[frameIndex].buffer;
		viewProjInfo.offset = 0;
		viewProjInfo.range = VK_WHOLE_SIZE;

		// Binding 7
		VkDescriptorBufferInfo materialsInfo{};
		materialsInfo.buffer = mMaterialBuffers[frameIndex].buffer;
		materialsInfo.offset = 0;
		materialsInfo.range = VK_WHOLE_SIZE;

		// Binding 6
		VkDescriptorBufferInfo lightsInfo{};
		lightsInfo.buffer = mPointLightUBOBuffers[frameIndex].buffer;
		lightsInfo.offset = 0;
		lightsInfo.range = VK_WHOLE_SIZE;

		// Binding 5
		VkDescriptorBufferInfo modelMatsInfo{};
		modelMatsInfo.buffer = mModelMatrixBuffers[frameIndex].buffer;
		modelMatsInfo.offset = 0;
		modelMatsInfo.range = VK_WHOLE_SIZE;

		// Binding 4 - Compute skinning stage 2 output to vertex shader read
		VkDescriptorBufferInfo comp2OutSkinMatInfo{};
		comp2OutSkinMatInfo.buffer = mShaderBoneMatrixBuffers[frameIndex].buffer;
		comp2OutSkinMatInfo.offset = 0;
		comp2OutSkinMatInfo.range = VK_WHOLE_SIZE;

		// Binding 3 - Compute skinning stage 2 input from stage 1
		VkDescriptorBufferInfo comp2InTRSInfo{};
		comp2InTRSInfo.buffer = mShaderTrsMatrixBuffers[frameIndex].buffer;
		comp2InTRSInfo.offset = 0;	
		comp2InTRSInfo.range = VK_WHOLE_SIZE;

		// Binding 2 - Compute skinning stage 1 input
		VkDescriptorBufferInfo comp1InSkinMatInfo{};
		comp1InSkinMatInfo.buffer = mNodeTransformBuffers[frameIndex].buffer;
		comp1InSkinMatInfo.offset = 0;
		comp1InSkinMatInfo.range = VK_WHOLE_SIZE;

		// Binding 1 - Procedural Generation uses this
		VkDescriptorImageInfo storageImageInfo{};
		storageImageInfo.sampler = VK_NULL_HANDLE;
		storageImageInfo.imageView = renderData.pgStorageImage[frameIndex].view;
		storageImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet boneMetaWriteDescriptorSet{};
		boneMetaWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		boneMetaWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		boneMetaWriteDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		boneMetaWriteDescriptorSet.dstBinding = 11;
		boneMetaWriteDescriptorSet.descriptorCount = 1;
		boneMetaWriteDescriptorSet.pBufferInfo = &boneMetaInfo;

		VkWriteDescriptorSet parentNodeWriteDescriptorSet{};
		parentNodeWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		parentNodeWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		parentNodeWriteDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		parentNodeWriteDescriptorSet.dstBinding = 10;
		parentNodeWriteDescriptorSet.descriptorCount = 1;
		parentNodeWriteDescriptorSet.pBufferInfo = &parentNodeInfo;

		VkWriteDescriptorSet boneOffsetWriteDescriptorSet{};
		boneOffsetWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		boneOffsetWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		boneOffsetWriteDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		boneOffsetWriteDescriptorSet.dstBinding = 9;
		boneOffsetWriteDescriptorSet.descriptorCount = 1;
		boneOffsetWriteDescriptorSet.pBufferInfo = &boneOffsetInfo;

		VkWriteDescriptorSet viewProjWriteDescriptorSet{};
		viewProjWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		viewProjWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		viewProjWriteDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		viewProjWriteDescriptorSet.dstBinding = 8;
		viewProjWriteDescriptorSet.descriptorCount = 1;
		viewProjWriteDescriptorSet.pBufferInfo = &viewProjInfo;

		VkWriteDescriptorSet materialWriteDescriptorSet{};
		materialWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		materialWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		materialWriteDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		materialWriteDescriptorSet.dstBinding = 7;
		materialWriteDescriptorSet.descriptorCount = 1;
		materialWriteDescriptorSet.pBufferInfo = &materialsInfo;

		VkWriteDescriptorSet lightWriteDescriptorSet{};
		lightWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		lightWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		lightWriteDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		lightWriteDescriptorSet.dstBinding = 6;
		lightWriteDescriptorSet.descriptorCount = 1;
		lightWriteDescriptorSet.pBufferInfo = &lightsInfo;

		VkWriteDescriptorSet modelMatDescriptorSet{};
		modelMatDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		modelMatDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		modelMatDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		modelMatDescriptorSet.dstBinding = 5;
		modelMatDescriptorSet.descriptorCount = 1;
		modelMatDescriptorSet.pBufferInfo = &modelMatsInfo;

		VkWriteDescriptorSet boneMatDescriptorSet{};
		boneMatDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		boneMatDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		boneMatDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		boneMatDescriptorSet.dstBinding = 4;
		boneMatDescriptorSet.descriptorCount = 1;
		boneMatDescriptorSet.pBufferInfo = &comp2OutSkinMatInfo;

		VkWriteDescriptorSet trsWriteDescriptorSet{};
		trsWriteDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		trsWriteDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		trsWriteDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		trsWriteDescriptorSet.dstBinding = 3;
		trsWriteDescriptorSet.descriptorCount = 1;
		trsWriteDescriptorSet.pBufferInfo = &comp2InTRSInfo;

		VkWriteDescriptorSet nodeTransformDescriptorSet{};
		nodeTransformDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		nodeTransformDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		nodeTransformDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		nodeTransformDescriptorSet.dstBinding = 2;
		nodeTransformDescriptorSet.descriptorCount = 1;
		nodeTransformDescriptorSet.pBufferInfo = &comp1InSkinMatInfo;

		VkWriteDescriptorSet texelDescriptorSet{};
		texelDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		texelDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		texelDescriptorSet.dstSet = renderData.rdAvengBindlessDescriptorSets[frameIndex];
		texelDescriptorSet.dstBinding = 1;
		texelDescriptorSet.descriptorCount = 1;
		texelDescriptorSet.pImageInfo = &storageImageInfo;

		VkWriteDescriptorSet writeDescriptorSets[11] = {
			viewProjWriteDescriptorSet,
			materialWriteDescriptorSet,
			lightWriteDescriptorSet,
			modelMatDescriptorSet,
			boneMatDescriptorSet,			// Compute
			trsWriteDescriptorSet,			// Compute
			nodeTransformDescriptorSet,		// Compute
			texelDescriptorSet,
			parentNodeWriteDescriptorSet,
			boneOffsetWriteDescriptorSet,
			boneMetaWriteDescriptorSet
		};

		vkUpdateDescriptorSets(
			engineDevice.device(), 
			ArraySize(writeDescriptorSets), 
			writeDescriptorSets, 
			0, 
			nullptr);

	}

	bool Renderer::createSSBOs() {

		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++) {

			if (!ShaderStorageBuffer::init(engineDevice, mShaderTrsMatrixBuffers[i], MapMode::GpuOnly, ResidentMode::GPU)) {
				Logger::log(1, "%s error: could not create TRS matrices SSBO\n", __FUNCTION__);
				return false;
			}

			if (!ShaderStorageBuffer::init(engineDevice, mShaderBoneMatrixBuffers[i], MapMode::GpuOnly, ResidentMode::GPU)) {
				Logger::log(1, "%s error: could not create bone matrix SSBO\n", __FUNCTION__);
				return false;
			}

			if (!ShaderStorageBuffer::init(engineDevice, mModelMatrixBuffers[i], MapMode::Persistent)) { // CPU Resident
				Logger::log(1, "%s error: could not create model root position SSBO\n", __FUNCTION__);
				return false;
			}

			if (!ShaderStorageBuffer::init(engineDevice, mNodeTransformBuffers[i], MapMode::Persistent)) { // CPU Resident
				Logger::log(1, "%s error: could not create node transform SSBO\n", __FUNCTION__);
				return false;
			}

			if (!ShaderStorageBuffer::init(engineDevice, mMaterialBuffers[i], MapMode::Persistent)) { // CPU Resident
				Logger::log(1, "%s error: could not create mMaterialBuffers SSBO\n", __FUNCTION__);
				return false;
			}

			// ToDo - Verify Residency
			if (!ShaderStorageBuffer::init(engineDevice, renderData.rdShaderBoneMatrixOffsetBuffers[i], MapMode::OnDemand, ResidentMode::CPU)) {
				Logger::log(1, "%s error: could not create rdShaderBoneMatrixOffsetBuffer SSBO\n", __FUNCTION__);
				return false;
			}
			
			// ToDo - Verify Residency
			if (!ShaderStorageBuffer::init(engineDevice, renderData.rdBoneParentNodeIndexBuffers[i], MapMode::OnDemand, ResidentMode::CPU)) {
				Logger::log(1, "%s error: could not create rdBoneParentMatrixBuffer SSBO\n", __FUNCTION__);
				return false;
			}
			
		}

		// Populate the shared view for the editor - for when it needs to update its descriptor sets
		renderData.matrixBuffersView.modelRootSSBOs = {
			mModelMatrixBuffers.data(),
			mModelMatrixBuffers.size()
		};

		renderData.matrixBuffersView.boneMatSSBOs = {
			mShaderBoneMatrixBuffers.data(),
			mShaderBoneMatrixBuffers.size()
		};

		renderData.matrixBuffersView.materialSSBOs = {
			mMaterialBuffers.data(),
			mMaterialBuffers.size()
		};

		return true;
	}

	void Renderer::updateBufferViews() {
	
		renderData.matrixBuffersView.modelRootSSBOs = {
			mModelMatrixBuffers.data(),
			mModelMatrixBuffers.size()
		};

		renderData.matrixBuffersView.boneMatSSBOs = {
			mShaderBoneMatrixBuffers.data(),
			mShaderBoneMatrixBuffers.size()
		};

		renderData.pointLightBufferView.viewPointLightUBOs = {
			mPointLightUBOBuffers.data(),
			mPointLightUBOBuffers.size()
		};
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

}