#include "ObjectRenderSystem.h"
#include "Game/Math/aveng_math.h"
#include "Utils/window_callbacks.h"
#include "Game/Player/GameplayFunctions.h"

namespace aveng {

	struct SimplePushConstantData
	{
		glm::mat4 modelMatrix{ 1.f };
		glm::mat4 normalMatrix{ 1.f };
	};

	ObjectRenderSystem::ObjectRenderSystem(EngineDevice& device, AvengWindow& window)
		: engineDevice{ device }, aveng_window{ window }
	{
#ifdef _DEBUG
		std::cout << "Running Dependency Checks..." << std::endl;
		DependencyChecks();
#endif
		// Initial camera position
		viewerObject.transform.translation.z = -5.5f;
		viewerObject.transform.translation.y = -2.5f;
	}

	void ObjectRenderSystem::initialize( VkRenderPass renderPass, VkDescriptorSetLayout globalDescriptorSetLayout, VkDescriptorSetLayout objDescriptorSetLayout)
	{
		VkDescriptorSetLayout descriptorSetLayouts[2] = { globalDescriptorSetLayout , objDescriptorSetLayout };
		createPipelineLayout(descriptorSetLayouts);
		createPipeline(renderPass);
	}

	ObjectRenderSystem::~ObjectRenderSystem()
	{
		vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
	}

	void ObjectRenderSystem::descriptorSetup() 
	{
		auto imageInfo = imageSystem.descriptorInfoForAllImages();

		/*
		 * Create Descriptor Pools
		 */
		descriptorPool = AvengDescriptorPool::Builder(engineDevice)
			.setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * 4) // Increased for lights descriptor sets
			// Type									// Max no. of descriptor sets
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT * 2) // Global + Lights buffers
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * imageInfo.size()) // Dependent upon no. of images/textures
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, SwapChain::MAX_FRAMES_IN_FLIGHT) // No. descriptor sets is implementation dependent
			.build();


		// Buffer vectors. One for each frame-in-flight
		u_GlobalBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		u_ObjBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		u_LightsBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		// Descriptor Set vectors. One for each frame-in-flight
		globalDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		objectDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		lightsDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		/*
		* Create Buffers
		*/
		for (int i = 0; i < u_GlobalBuffers.size(); i++) {
			u_GlobalBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(GlobalUbo), // The size can remain static because every object accesses its data at the same location.
				1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			u_GlobalBuffers[i]->map();
		}

		for (int i = 0; i < u_LightsBuffers.size(); i++) {
			u_LightsBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(LightsUbo),
				1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			u_LightsBuffers[i]->map();
		}
		for (int i = 0; i < u_ObjBuffers.size(); i++) {
			u_ObjBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				calculateDynamicUBOStride() * num_objects, // Total buffer size: stride × number of objects
				1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
				calculateDynamicUBOStride()); // Pass stride as alignment parameter
			u_ObjBuffers[i]->map();
		}
		/*
		* Create Descriptor Set Layouts
		*/
		// Descriptor Layout 0 -- Global
		std::unique_ptr<AvengDescriptorSetLayout> globalDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, imageInfo.size())	// Combined image samplers use 1 descriptor for each image
			.build();

		// Descriptor Set 1 -- Per object
		std::unique_ptr<AvengDescriptorSetLayout> objDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.build();

		// Descriptor Set 2 -- Lights
		std::unique_ptr<AvengDescriptorSetLayout> lightsDescriptorSetLayout =
			AvengDescriptorSetLayout::Builder(engineDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS, 1)
			.build();

		// Write each descriptor set, once for each swapchain frame
		for (int i = 0; i < SwapChain::MAX_FRAMES_IN_FLIGHT; i++)
		{
			// Global descriptor set
			auto globalBufferInfo = u_GlobalBuffers[i]->descriptorInfo(sizeof(GlobalUbo), 0);
			AvengDescriptorSetWriter(*globalDescriptorSetLayout, *descriptorPool)
				.writeBuffer(0, &globalBufferInfo)	// First Binding descriptor: Buffer
				.writeImage(1, imageInfo.data(), imageInfo.size()) // Second Binding descriptor: Image
				.build(globalDescriptorSets[i]);

			// Object descriptor set
			auto objBufferInfo = u_ObjBuffers[i]->descriptorInfo(sizeof(ObjectRenderSystem::ObjectUniformData), 0); // 0 here refers to the position (in the buffer) to begin reading from before any offsets are applied
			AvengDescriptorSetWriter(*objDescriptorSetLayout, *descriptorPool)
				.writeBuffer(0, &objBufferInfo)
				.build(objectDescriptorSets[i]);

			// Lights descriptor set
			auto lightsBufferInfo = u_LightsBuffers[i]->descriptorInfo(sizeof(LightsUbo), 0);
			AvengDescriptorSetWriter(*lightsDescriptorSetLayout, *descriptorPool)
				.writeBuffer(0, &lightsBufferInfo)
				.build(lightsDescriptorSets[i]);
		}

		initialize(
			renderer.getSwapChainRenderPass(),
			globalDescriptorSetLayout->getDescriptorSetLayout(),
			objDescriptorSetLayout->getDescriptorSetLayout()
		);

		pointLightSystem.initialize(
			renderer.getSwapChainRenderPass(),
			globalDescriptorSetLayout->getDescriptorSetLayout(),
			lightsDescriptorSetLayout->getDescriptorSetLayout()
		);

		// GUI
		//aveng_imgui.init(
		//	aveng_window,
		//	renderer.getSwapChainRenderPass(),
		//	renderer.getImageCount()
		//);
	}

	/*
	 * Setup of the pipeline layout. 
	 * Here we include our Push Constant information
	 * as well as our descriptor set layouts.
	 */
	void ObjectRenderSystem::createPipelineLayout(VkDescriptorSetLayout* descriptorSetLayouts)
	{

		// Initialize push constant range(s)
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(SimplePushConstantData);	// Must be a multiple of 4

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
		pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutInfo.setLayoutCount = 2;										// How many descriptor set layouts are to be hooked into the pipeline
		pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;						// a pointer to an array of VkDescriptorSetLayout objects.
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

		// Create the pipeline layout, updating our pipelineLayout member.
		if (vkCreatePipelineLayout(engineDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) 
		{
			throw std::runtime_error("failed to create pipeline layout!");
		}

	}

	/*
	* Call to the construction of a Graphics Pipeline.
	* Note that shader filepaths are relative to the GFXPipeline.cpp file.
	*/
	void ObjectRenderSystem::createPipeline(VkRenderPass renderPass)
	{
		// Initialize the pipeline 
		assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");

		PipelineConfig pipelineConfig{};
		GFXPipeline::defaultPipelineConfig(pipelineConfig);
		pipelineConfig.renderPass = renderPass;		
		pipelineConfig.pipelineLayout = pipelineLayout;

		// A GFXPipeline
		gfxPipeline = std::make_unique<GFXPipeline>(
			engineDevice,
			"shaders/simple_shader.vert.spv",
			"shaders/simple_shader.frag.spv",
			pipelineConfig
		);

		// Another GFXPipeline
		gfxPipeline2 = std::make_unique<GFXPipeline>(
			engineDevice,
			"shaders/simple_shader2.vert.spv",
			"shaders/simple_shader2.frag.spv",
			pipelineConfig
		);
	}

	void ObjectRenderSystem::render(float frameTime, FrameContent& frame_content /*, AvengBuffer& u_ObjBuffer*/)
	{

		// Clear Color for now
		frame_content.rgb = glm::vec3(0.001f, 0.008f, 0.06f); // Cool, dark midnight blue
		
		// 1s tick, convenient
		if (last_sec != game_data.sec) {
			last_sec = game_data.sec;
			std::cout << "Tick..." << std::endl;
		}

		updateCamera(frameTime, viewerObject, keyboardController);
		updateData(frameTime);

		// Get a command buffer for this frame
		VkCommandBuffer commandBuffer = renderer.beginFrame();
		if (!commandBuffer) {
			throw std::runtime_error("Command Buffer failed to initialize for this render pass.");
		}
		int frameIndex = renderer.getFrameIndex();
		// Render
		renderer.beginSwapChainRenderPass(commandBuffer, frame_content.rgb);

		// Pack our vertex shader uniform buffer
		u_GlobalData.projection = aveng_camera.getProjection();
		u_GlobalData.view = aveng_camera.getView();

		// Update our global uniform buffer 
		u_GlobalBuffers[frameIndex]->writeToBuffer(&u_GlobalData);
		u_GlobalBuffers[frameIndex]->flush();

		// Update our lights uniform buffer
		u_LightsBuffers[frameIndex]->writeToBuffer(&u_LightsData);
		u_LightsBuffers[frameIndex]->flush();

		// Bind our current pipeline configuration
		switch (game_data.cur_pipe)
		{
			case 98: gfxPipeline->bind(commandBuffer);  break;
			case 99: gfxPipeline2->bind(commandBuffer); break;
			default:
				gfxPipeline->bind(commandBuffer); // 0
		}

		vkCmdBindDescriptorSets(
			commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0,
			1,
			&globalDescriptorSets[frameIndex],
			0,
			nullptr);

		// updateData(frame_content.appObjects.size(), frame_content.frameTime, data);

		/*
		* Thread object bind/draw calls here
		*/
		int i = 0;
		for (auto& kv : sceneLoader.getAppObjects())
		{
			ObjectUniformData u_ObjData{ kv.second.get_texture() };	// Contains texture index

			// Push Constant Data
			SimplePushConstantData push{};
			push.modelMatrix  = kv.second.transform._mat4();
			push.normalMatrix = kv.second.transform.normalMatrix();

			// Instead of manual offset calculation, use these built-in methods:
			u_ObjBuffers[frameIndex]->writeToIndex(&u_ObjData, i);  // Handles stride automatically
			u_ObjBuffers[frameIndex]->flushIndex(i);               // Flushes correct range
			auto descriptorInfo = u_ObjBuffers[frameIndex]->descriptorInfoForIndex(i); // Perfect offset
			uint32_t dynamicOffset = static_cast<uint32_t>(descriptorInfo.offset); // Extract offset for binding

			vkCmdBindDescriptorSets(
				commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				pipelineLayout,
				1,
				1,
				&objectDescriptorSets[frameIndex],
				1,
				&dynamicOffset);

			vkCmdPushConstants(
				commandBuffer,
				pipelineLayout,
				VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
				0,
				sizeof(SimplePushConstantData),
				&push);

			kv.second.model->bind(commandBuffer);
			kv.second.model->draw(commandBuffer);

			i++; // Increment AFTER using the index
		}

		pointLightSystem.render(globalDescriptorSets[frameIndex], lightsDescriptorSets[frameIndex], commandBuffer, u_LightsData.numLights);
		editor.render(commandBuffer);

		renderer.endSwapChainRenderPass(commandBuffer);
		renderer.endFrame();

	}

	void ObjectRenderSystem::updateCamera(float frameTime, AvengAppObject& viewerObject, KeyboardController& keyboardController)
	{
		aspect = getAspectRatio();
		// Updates the viewer object transform component based on key input, proportional to the time elapsed since the last frame
		keyboardController.moveCameraXZ(aveng_window.getGLFWwindow(), frameTime);
		aveng_camera.setViewYXZ(viewerObject.transform.translation + glm::vec3(0.f, 0.f, -.80f), viewerObject.transform.rotation + glm::vec3());
		aveng_camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 1000.f);
	}

	void ObjectRenderSystem::updateData(float frameTime)
	{

		game_data.num_objs = 2;
		game_data.cur_pipe = WindowCallbacks::getCurPipeline();
		game_data.dt = frameTime;
		game_data.camera_modPI = viewerObject.transform.modPI;

		game_data.cameraView = aveng_camera.getCameraView();
		game_data.cameraPos = viewerObject.transform.translation;
		game_data.cameraRot = viewerObject.transform.rotation;
		game_data.fly_mode = WindowCallbacks::flightMode;

	}

	void ObjectRenderSystem::DependencyChecks() {

		/*
		* Implementation dependency and utilization checks.
		*/

		// Image sampler support
		VkPhysicalDeviceFeatures m;
		vkGetPhysicalDeviceFeatures(engineDevice.physicalDevice(), &m);
		if (!m.shaderSampledImageArrayDynamicIndexing) {
			throw std::runtime_error("Your hardware does not support this specific VulkanAPI implementation. Sorry!");
		}
		
		// Validate ObjectUniformData alignment requirements
		size_t objectSize = sizeof(ObjectUniformData);
		size_t minAlignment = engineDevice.properties.limits.minUniformBufferOffsetAlignment;
		
		std::cout << "ObjectUniformData size: " << objectSize << " bytes" << std::endl;
		std::cout << "minUniformBufferOffsetAlignment: " << minAlignment << " bytes" << std::endl;
		
		// Check if ObjectUniformData fits within one alignment boundary (recommended for performance)
		if (objectSize > minAlignment) {
			std::cout << "Warning: ObjectUniformData (" << objectSize << " bytes) exceeds minUniformBufferOffsetAlignment (" 
			          << minAlignment << " bytes). This may impact performance." << std::endl;
		}
		
		// Calculate how many alignment boundaries our object spans
		size_t alignmentBoundaries = (objectSize + minAlignment - 1) / minAlignment;
		std::cout << "ObjectUniformData spans " << alignmentBoundaries << " alignment boundaries" << std::endl;

	}

	/**
	 * 
	 * This function calculates the stride for the PER OBJECT dynamic uniform buffer.
	 */
	size_t ObjectRenderSystem::calculateDynamicUBOStride() const
	{
		size_t objectSize = sizeof(ObjectUniformData);
		size_t minAlignment = engineDevice.properties.limits.minUniformBufferOffsetAlignment;
		
		// Calculate stride: round up to next alignment boundary
		// Formula: ((size + alignment - 1) / alignment) * alignment
		size_t stride = ((objectSize + minAlignment - 1) / minAlignment) * minAlignment;
		
		return stride;
	}

	void ObjectRenderSystem::addLight(const glm::vec3& position, const glm::vec3& color, float intensity, float radius)
	{
		if (u_LightsData.numLights >= LightsUbo::MAX_LIGHTS) {
			std::cout << "Warning: Maximum number of lights (" << LightsUbo::MAX_LIGHTS << ") reached. Cannot add more lights." << std::endl;
			return;
		}

		PointLight& light = u_LightsData.lights[u_LightsData.numLights];
		light.position = glm::vec4(position, radius);
		light.color = glm::vec4(color, intensity);
		u_LightsData.numLights++;
	}

	void ObjectRenderSystem::clearLights()
	{
		u_LightsData.numLights = 0;
		// Zero out the lights array for clean state
		memset(u_LightsData.lights, 0, sizeof(u_LightsData.lights));
	}

	void ObjectRenderSystem::loadGame(const std::string& scenePath)
	{
		sceneLoader.load(scenePath.c_str(), engineDevice);
		
		// Update the number of objects for buffer allocation
		setNumObjects(static_cast<int>(sceneLoader.getObjectCount()));
		
		std::cout << "Loaded scene with " << sceneLoader.getObjectCount() << " objects" << std::endl;
	}

} //