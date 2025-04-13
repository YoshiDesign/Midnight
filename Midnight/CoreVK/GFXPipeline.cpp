#include "GFXPipeline.h"
#include "../Core/aveng_model.h"

#include <cassert>
#include <iostream>
#include <fstream>
#include <stdexcept>

namespace aveng {

	// Ctor
	GFXPipeline::GFXPipeline(
		EngineDevice& device,
		const std::string& vertFilepath,
		const std::string& fragFilepath,
		const PipelineConfig& config
	) 
		: engDevice {device}
	{
		createGFXPipeline(vertFilepath, fragFilepath, config);
	}

	GFXPipeline::~GFXPipeline()
	{
		// Destroy!
		vkDestroyShaderModule(engDevice.device(), vertShaderModule, nullptr);
		vkDestroyShaderModule(engDevice.device(), fragShaderModule, nullptr);
		vkDestroyPipeline(engDevice.device(), graphicsPipeline, nullptr);
	}

	/**
	 *	Read our vertex and fragment code
	 */
	void GFXPipeline::createGFXPipeline(
		const std::string& vertFilepath,
		const std::string& fragFilepath,
		const PipelineConfig& configInfo
	)
	{
		assert(
			configInfo.pipelineLayout != VK_NULL_HANDLE &&
			"Cannot create graphics pipeline: no pipelineLayout provided in configInfo");
		assert(
			configInfo.renderPass != VK_NULL_HANDLE &&
			"Cannot create graphics pipeline: no renderPass provided in configInfo");

		auto vertCode = readFile(vertFilepath);
		auto fragCode = readFile(fragFilepath);

		// Debug vectors
		//std::cout << "Vertex Shader: " << vertCode.size() << std::endl;
		//std::cout << "FragmentShader: " << fragCode.size() << std::endl;

		// Initialize shader modules!
		createShaderModule(vertCode, &vertShaderModule);
		createShaderModule(fragCode, &fragShaderModule);

		VkPipelineShaderStageCreateInfo shaderStages[2];

		// Vertex
		shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		shaderStages[0].module = vertShaderModule;
		shaderStages[0].pName = "main";		// Name of the entry function in our vertex shader
		shaderStages[0].flags = 0;
		shaderStages[0].pNext = nullptr;
		shaderStages[0].pSpecializationInfo = nullptr;

		// Fragment
		shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		shaderStages[1].module = fragShaderModule;
		// Name of the entry function in our fragment shader
		shaderStages[1].pName = "main";		// Name of the entry function in our fragment shader
		shaderStages[1].flags = 0;
		shaderStages[1].pNext = nullptr;
		shaderStages[1].pSpecializationInfo = nullptr;

		auto& bindingDescriptions = configInfo.bindingDescriptions;
		auto& attributeDescriptions = configInfo.attributeDescriptions;

		std::cout << "GFXPipeline (attributeDescriptions.size()):\t" << attributeDescriptions.size() << std::endl;
		std::cout << "GFXPipeline (bindingDescriptions.size()):\t" << bindingDescriptions.size() << std::endl;

		// Struct to describe how we interperet our vertex buffer data as initial input into our pipeline
		VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
		vertexInputInfo.sType							= VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());	// Updated
		vertexInputInfo.vertexBindingDescriptionCount	= static_cast<uint32_t>(bindingDescriptions.size());		// Updated
		vertexInputInfo.pVertexAttributeDescriptions	= attributeDescriptions.data();
		vertexInputInfo.pVertexBindingDescriptions		= bindingDescriptions.data();
		// vertexInputInfo.flags							= 0;	
		// vertexInputInfo.pNext							= nullptr;

		// Combine our viewport and our scissor. On some GFX Cards you can have multiple viewports/scissors
		//VkPipelineViewportStateCreateInfo viewportInfo{};
		//viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		//viewportInfo.viewportCount = 1;
		//viewportInfo.pViewports = &configInfo.viewport;
		//viewportInfo.scissorCount = 1;
		//viewportInfo.pScissors = &configInfo.scissor;
		//viewportInfo.flags = 0;
		//viewportInfo.pNext = nullptr;

		//
		VkGraphicsPipelineCreateInfo pipelineInfo{};
		pipelineInfo.sType				= VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.stageCount			= 2;
		pipelineInfo.pStages			= shaderStages;
		pipelineInfo.pVertexInputState	= &vertexInputInfo;;
		// pipelineInfo.flags				= 0;
		// pipelineInfo.pNext				= nullptr;
		
		// Apply the pipeline creation information to the config information we already setup
		pipelineInfo.pInputAssemblyState	= &configInfo.inputAssemblyInfo;
		pipelineInfo.pViewportState			= &configInfo.viewportInfo;
		pipelineInfo.pMultisampleState		= &configInfo.multisampleInfo;
		pipelineInfo.pRasterizationState	= &configInfo.rasterizationInfo;
		pipelineInfo.pColorBlendState		= &configInfo.colorBlendInfo;
		pipelineInfo.pDepthStencilState		= &configInfo.depthStencilInfo;
		pipelineInfo.pDynamicState			= &configInfo.dynamicStateInfo;

		pipelineInfo.layout		= configInfo.pipelineLayout;
		pipelineInfo.renderPass = configInfo.renderPass;
		pipelineInfo.subpass	= configInfo.subpass;

		pipelineInfo.basePipelineIndex = -1;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

		if (vkCreateGraphicsPipelines(engDevice.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
		{
			throw std::runtime_error("failed to create graphics pipeline");
		}

	}

	void GFXPipeline::bind(VkCommandBuffer commandBuffer)
	{

		// Establish the type of pipeline we're using e.g. Computer only, graphics, ray-tracing
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);
	}

	/**
	 *	Read our shader binary into a vector
	 */
	std::vector<char> GFXPipeline::readFile(const std::string& filepath)
	{ 
		// Seek to end of `filepath` by moving cursor via bit-flag std::ios::ate (similar to calling fseek), and read in binary
		// We're seeking the end immediately to get the size.
		// Reading as binary avoids any unwanted text transformation.
		std::ifstream file{ filepath, std::ios::ate | std::ios::binary };

		// Check for success
		if (!file.is_open())
		{
			throw std::runtime_error("Failed to open file: " + filepath);
		}

		// Get the size
		size_t fileSize = static_cast<size_t>(file.tellg());

		// Initialize a vector to the size of the binary
		std::vector<char> buffer(fileSize);

		// Read our shader program binary into the vector's array
		file.seekg(0);
		file.read(buffer.data(), fileSize);

		file.close();

		return buffer;
	}

	void GFXPipeline::createShaderModule(const std::vector<char>& code, VkShaderModule* shaderModule)
	{
		// Struct which carries our parameters
		VkShaderModuleCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());	// Avoid type mismatch - Alignment is asserted thanks to vector's implementation

		if ( vkCreateShaderModule(engDevice.device(), &createInfo, nullptr, shaderModule) != VK_SUCCESS )
		{
			throw std::runtime_error("Failed to create shader module.");
		}
	}

	/**
	* Initialize struct PipelineConfig
	*/
	void GFXPipeline::defaultPipelineConfig(PipelineConfig& configInfo)
	{

		// This is the first stage of our pipeline. It takes a list of vertices and groups them into geometry
		configInfo.inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

		configInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;	// SEE: enum VkPrimitiveTopology - This particular config answers how we will treat our 
																						// groups of vertices. This constant tells the pipeline assembler that every 3 vertices make 
																						// up the points of a triangle. Not optimized for overlapping vertices.

		configInfo.inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;					// True would mean we're using a strip topology (optimized for connecting triangles by shared vertices)

		configInfo.viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		configInfo.viewportInfo.viewportCount	= 1;
		configInfo.viewportInfo.pViewports		= nullptr;
		configInfo.viewportInfo.scissorCount	= 1;
		configInfo.viewportInfo.pScissors		= nullptr;

		// Rasterization phase
		configInfo.rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		configInfo.rasterizationInfo.depthClampEnable			= VK_FALSE;
		configInfo.rasterizationInfo.rasterizerDiscardEnable	= VK_FALSE;	// Cuts off the rest of the pipeline
		configInfo.rasterizationInfo.polygonMode				= VK_POLYGON_MODE_FILL;	// Draw corners only? edges only? fill it in?
		configInfo.rasterizationInfo.lineWidth					= 1.0f;
		configInfo.rasterizationInfo.cullMode					= VK_CULL_MODE_NONE;			// Culling options
		configInfo.rasterizationInfo.frontFace					= VK_FRONT_FACE_CLOCKWISE;
		configInfo.rasterizationInfo.depthBiasEnable			= VK_FALSE;
		configInfo.rasterizationInfo.depthBiasConstantFactor	= 0.0f;  // Optional
		configInfo.rasterizationInfo.depthBiasClamp				= 0.0f;           // Optional
		configInfo.rasterizationInfo.depthBiasSlopeFactor		= 0.0f;     // Optional

		// Management of MSAA (Multisample Anti-Aliasing)
		configInfo.multisampleInfo.sType					= VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		configInfo.multisampleInfo.sampleShadingEnable		= VK_FALSE;
		configInfo.multisampleInfo.rasterizationSamples		= VK_SAMPLE_COUNT_1_BIT;
		configInfo.multisampleInfo.minSampleShading			= 1.0f;           // Optional
		configInfo.multisampleInfo.pSampleMask				= nullptr;             // Optional
		configInfo.multisampleInfo.alphaToCoverageEnable	= VK_FALSE;  // Optional
		configInfo.multisampleInfo.alphaToOneEnable			= VK_FALSE;       // Optional

		// Color blending
		configInfo.colorBlendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT;
		configInfo.colorBlendAttachment.blendEnable = VK_FALSE;	// Determine how we mix colors of our current output with values that already reside in the frame buffer, if any
		configInfo.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
		configInfo.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
		configInfo.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;              // Optional
		configInfo.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;   // Optional
		configInfo.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;  // Optional
		configInfo.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;              // Optional

		// Depth buffer for depth testing. An attachment to our frame buffer
		configInfo.colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		configInfo.colorBlendInfo.logicOpEnable = VK_FALSE;
		configInfo.colorBlendInfo.logicOp = VK_LOGIC_OP_COPY;  // Optional
		configInfo.colorBlendInfo.attachmentCount = 1;
		configInfo.colorBlendInfo.pAttachments = &configInfo.colorBlendAttachment;
		configInfo.colorBlendInfo.blendConstants[0] = 0.0f;  // Optional
		configInfo.colorBlendInfo.blendConstants[1] = 0.0f;  // Optional
		configInfo.colorBlendInfo.blendConstants[2] = 0.0f;  // Optional
		configInfo.colorBlendInfo.blendConstants[3] = 0.0f;  // Optional

		configInfo.depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		configInfo.depthStencilInfo.depthTestEnable = VK_TRUE;
		configInfo.depthStencilInfo.depthWriteEnable = VK_TRUE;
		configInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
		configInfo.depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
		configInfo.depthStencilInfo.minDepthBounds = 0.0f;  // Optional
		configInfo.depthStencilInfo.maxDepthBounds = 1.0f;  // Optional
		configInfo.depthStencilInfo.stencilTestEnable = VK_FALSE;
		configInfo.depthStencilInfo.front = {};  // Optional
		configInfo.depthStencilInfo.back = {};   // Optional

		// Configure the pipeline to expect a dynamic viewport and dynamic scissor at a later time
		configInfo.dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		configInfo.dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		configInfo.dynamicStateInfo.pDynamicStates = configInfo.dynamicStateEnables.data();
		configInfo.dynamicStateInfo.dynamicStateCount =
			static_cast<uint32_t>(configInfo.dynamicStateEnables.size());
		configInfo.dynamicStateInfo.flags = 0;

		configInfo.bindingDescriptions = AvengModel::Vertex::getBindingDescriptions();
		configInfo.attributeDescriptions = AvengModel::Vertex::getAttributeDescriptions();
		
	}

}