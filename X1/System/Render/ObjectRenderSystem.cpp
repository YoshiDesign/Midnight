#include "ObjectRenderSystem.h"
#include "Game/Math/aveng_math.h"
#include "System/Events/window_callbacks.h"
#include "Game/Player/GameplayFunctions.h"

namespace aveng {

	struct SimplePushConstantData
	{
		glm::mat4 modelMatrix{ 1.f };
		glm::mat4 normalMatrix{ 1.f };
	};

	//ObjectRenderSystem::ObjectRenderSystem()
	//{
	//	descriptorSetup();
	//}

	ObjectRenderSystem::ObjectRenderSystem(EngineDevice& device, AvengAppObject& viewer, AvengWindow& window)
		: engineDevice{ device }, viewerObject{ viewer }, aveng_window{ window }
	{
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
			.setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT * 3)
			// Type									// Max no. of descriptor sets
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SwapChain::MAX_FRAMES_IN_FLIGHT) // No. descriptor sets is implementation dependent
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SwapChain::MAX_FRAMES_IN_FLIGHT * imageInfo.size()) // Dependent upon no. of images/textures
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, SwapChain::MAX_FRAMES_IN_FLIGHT) // No. descriptor sets is implementation dependent
			.build();


		// Buffer vectors. One for each frame-in-flight
		u_GlobalBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		u_ObjBuffers.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

		// Descriptor Set vectors. One for each frame-in-flight
		globalDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
		objectDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);

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

		for (int i = 0; i < u_ObjBuffers.size(); i++) {
			u_ObjBuffers[i] = std::make_unique<AvengBuffer>(engineDevice,
				sizeof(/*ObjectRenderSystem:: */ ObjectUniformData) * engineDevice.properties.limits.minUniformBufferOffsetAlignment * num_objects, // appObjects.size(), // The size <VkDeviceSize> of the entire dynamic uniform buffer. Each object has a particular dynamic offset from which to access its data
				1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
				engineDevice.properties.limits.minUniformBufferOffsetAlignment);
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
		}

		initialize(
			renderer.getSwapChainRenderPass(),
			globalDescriptorSetLayout->getDescriptorSetLayout(),
			objDescriptorSetLayout->getDescriptorSetLayout()
		);

		pointLightSystem.initialize(
			renderer.getSwapChainRenderPass(),
			globalDescriptorSetLayout->getDescriptorSetLayout()
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

	void ObjectRenderSystem::render(FrameContent& frame_content, GameData& data /*, AvengBuffer& u_ObjBuffer*/)
	{

		// 1s tick, convenient
		if (last_sec != data.sec) {
			last_sec = data.sec;
			std::cout << "Tick..." << std::endl;
		}

		// Get a command buffer for this frame
		VkCommandBuffer commandBuffer = renderer.beginFrame();
		if (!commandBuffer) {
			throw std::runtime_error("Command Buffer failed to initialize for this render pass.");
		}
		int frameIndex = renderer.getFrameIndex();
		// Render
		renderer.beginSwapChainRenderPass(commandBuffer);

		// Pack our vertex shader uniform buffer
		u_GlobalData.projection = frame_content.camera.getProjection();
		u_GlobalData.view = frame_content.camera.getView();

		// Update our global uniform buffer 
		u_GlobalBuffers[frameIndex]->writeToBuffer(&u_GlobalData);
		u_GlobalBuffers[frameIndex]->flush();

		// Bind our current pipeline configuration
		switch (data.cur_pipe)
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

		updateData(frame_content.appObjects.size(), frame_content.frameTime, data);

		/*
		* Thread object bind/draw calls here
		*/
		int i = 0;
		for (auto& kv : frame_content.appObjects)
		{
			i++;
			ObjectUniformData u_ObjData{ kv.second.get_texture() };	// Contains texture index

			// Push Constant Data
			SimplePushConstantData push{};
			push.modelMatrix  = kv.second.transform._mat4();
			push.normalMatrix = kv.second.transform.normalMatrix();

			uint32_t dynamicOffset = engineDevice.properties.limits.minUniformBufferOffsetAlignment * i;
			if (dynamicOffset > engineDevice.properties.limits.maxUniformBufferRange) {
			    //This will (should) never occur because we're taking measurments from the same source when instantiating the dynamic uniform buffer
				// Remove this for any release builds
				DEBUG("Max Uniform Buffer Range Exceeded.");
				throw std::runtime_error("Attempting to allocate buffer beyond device uniform buffer memory limit.");
			}

			// Bind the descriptor set for our pixel (fragment) shader
			u_ObjBuffers[frameIndex]->writeToBuffer(&u_ObjData, sizeof(ObjectUniformData), dynamicOffset);
			u_ObjBuffers[frameIndex]->flush();

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

		}

		pointLightSystem.render(globalDescriptorSets[frameIndex], commandBuffer);

		//aveng_imgui.newFrame();
		//aveng_imgui.runGUI(data);
		//aveng_imgui.render(commandBuffer);

		renderer.endSwapChainRenderPass(commandBuffer);
		renderer.endFrame();

	}

	void ObjectRenderSystem::updateData(size_t size, float frameTime, GameData& data)
	{

		data.num_objs = size;
		data.cur_pipe = WindowCallbacks::getCurPipeline();
		data.dt = frameTime;
		data.camera_modPI = viewerObject.transform.modPI;

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
		// If ObjectUniformData exceeds minUniformBufferOffsetAlignment we'll need to handle it.
		if (sizeof(ObjectRenderSystem::ObjectUniformData) > engineDevice.properties.limits.minUniformBufferOffsetAlignment)
		{
			throw std::runtime_error("ObjectUniformData is larger than the minimum offset alignment. Uniform Data size needs to be updated.");
		}

	}

} //