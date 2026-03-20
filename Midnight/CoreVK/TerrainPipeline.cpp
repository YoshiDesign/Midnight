#include "TerrainPipeline.h"
#include <vector>
#include <cstddef>
#include "Shader.h"
#include "Utils/Logger.h"
#include "CoreVK/VkRenderData.h"
#include "Utils/glm_includes.h"

namespace aveng {


    bool TerrainPipeline::init(EngineDevice& engineDevice, VkPipelineLayout& pipelineLayout, VkPipeline& pipeline,
        VkRenderPass renderpass, uint32_t numColorAttachments, std::string vertexShaderFilename, std::string fragmentShaderFilename) {
        /* shader */
        VkShaderModule vertexModule = Shader::loadShader(engineDevice.device(), vertexShaderFilename);
        VkShaderModule fragmentModule = Shader::loadShader(engineDevice.device(), fragmentShaderFilename);

        if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE) {
            Logger::log(1, "%s error: could not load shaders\n", __FUNCTION__);
            Shader::cleanup(engineDevice.device(), vertexModule);
            Shader::cleanup(engineDevice.device(), fragmentModule);
            return false;
        }

        VkPipelineShaderStageCreateInfo vertexStageInfo{};
        vertexStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertexStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertexStageInfo.module = vertexModule;
        vertexStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragmentStageInfo{};
        fragmentStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragmentStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragmentStageInfo.module = fragmentModule;
        fragmentStageInfo.pName = "main";

        std::vector<VkPipelineShaderStageCreateInfo> shaderStagesInfo = { vertexStageInfo, fragmentStageInfo };

        /* assemble the graphics pipeline itself */
        std::vector<VkVertexInputBindingDescription> vertexBindings = {
            { 0, static_cast<uint32_t>(sizeof(glm::vec3)), VK_VERTEX_INPUT_RATE_VERTEX }
        };

        VkVertexInputAttributeDescription positionAttribute{};
        positionAttribute.binding = 0;
        positionAttribute.location = 0;
        positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
        positionAttribute.offset = 0;

        //VkVertexInputAttributeDescription colorAttribute{};
        //colorAttribute.binding = 0;
        //colorAttribute.location = 1;
        //colorAttribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        //colorAttribute.offset = static_cast<uint32_t>(offsetof(VkVertex, color));

        //VkVertexInputAttributeDescription normalAttribute{};
        //normalAttribute.binding = 0;
        //normalAttribute.location = 2;
        //normalAttribute.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        //normalAttribute.offset = static_cast<uint32_t>(offsetof(VkVertex, normal));

        std::vector<VkVertexInputAttributeDescription> attributes{};
        attributes.emplace_back(positionAttribute);
        //attributes.emplace_back(colorAttribute);
        //attributes.emplace_back(normalAttribute);

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindings.size());
        vertexInputInfo.pVertexBindingDescriptions = vertexBindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

        /* empty structs here, will be set in renderer dynamically */
        VkViewport viewport{};
        VkRect2D scissor{};

        VkPipelineViewportStateCreateInfo viewportStateInfo{};
        viewportStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateInfo.viewportCount = 1;
        viewportStateInfo.pViewports = &viewport;
        viewportStateInfo.scissorCount = 1;
        viewportStateInfo.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizerInfo{};
        rasterizerInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizerInfo.depthClampEnable = VK_FALSE;
        rasterizerInfo.rasterizerDiscardEnable = VK_FALSE;
        rasterizerInfo.polygonMode = VK_POLYGON_MODE_LINE;
        rasterizerInfo.lineWidth = 1.0f;
        rasterizerInfo.cullMode = VK_CULL_MODE_BACK_BIT;
        /* use CCW winding */
        rasterizerInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizerInfo.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisamplingInfo{};
        multisamplingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisamplingInfo.sampleShadingEnable = VK_FALSE;
        multisamplingInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments{};
        for (int i = 0; i < numColorAttachments; ++i) {
            VkPipelineColorBlendAttachmentState colorBlendAttachment{};
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            colorBlendAttachment.blendEnable = VK_FALSE;
            colorBlendAttachments.emplace_back(colorBlendAttachment);
        }

        VkPipelineColorBlendStateCreateInfo colorBlendingInfo{};
        colorBlendingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlendingInfo.logicOpEnable = VK_FALSE;
        colorBlendingInfo.logicOp = VK_LOGIC_OP_COPY;
        colorBlendingInfo.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
        colorBlendingInfo.pAttachments = colorBlendAttachments.data();
        colorBlendingInfo.blendConstants[0] = 0.0f;
        colorBlendingInfo.blendConstants[1] = 0.0f;
        colorBlendingInfo.blendConstants[2] = 0.0f;
        colorBlendingInfo.blendConstants[3] = 0.0f;

        VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
        depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencilInfo.depthTestEnable = VK_TRUE;
        depthStencilInfo.depthWriteEnable = VK_TRUE;
        depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        depthStencilInfo.depthBoundsTestEnable = VK_FALSE;
        depthStencilInfo.minDepthBounds = 0.0f;
        depthStencilInfo.maxDepthBounds = 1.0f;
        depthStencilInfo.stencilTestEnable = VK_FALSE;

        std::vector<VkDynamicState> dynStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

        VkPipelineDynamicStateCreateInfo dynStatesInfo{};
        dynStatesInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynStatesInfo.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
        dynStatesInfo.pDynamicStates = dynStates.data();

        VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStagesInfo.size());
        pipelineCreateInfo.pStages = shaderStagesInfo.data();
        pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
        pipelineCreateInfo.pInputAssemblyState = &inputAssemblyInfo;
        pipelineCreateInfo.pViewportState = &viewportStateInfo;
        pipelineCreateInfo.pRasterizationState = &rasterizerInfo;
        pipelineCreateInfo.pMultisampleState = &multisamplingInfo;
        pipelineCreateInfo.pColorBlendState = &colorBlendingInfo;
        pipelineCreateInfo.pDepthStencilState = &depthStencilInfo;
        pipelineCreateInfo.pDynamicState = &dynStatesInfo;
        pipelineCreateInfo.layout = pipelineLayout;
        pipelineCreateInfo.renderPass = renderpass;
        pipelineCreateInfo.subpass = 0;
        pipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE;

        VkResult result = vkCreateGraphicsPipelines(engineDevice.device(), VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not create rendering pipeline (error: %i)\n", __FUNCTION__, result);
            Shader::cleanup(engineDevice.device(), vertexModule);
            Shader::cleanup(engineDevice.device(), fragmentModule);
            vkDestroyPipelineLayout(engineDevice.device(), pipelineLayout, nullptr);
            return false;
        }

        /* it is save to destroy the shader modules after pipeline has been created */
        Shader::cleanup(engineDevice.device(), vertexModule);
        Shader::cleanup(engineDevice.device(), fragmentModule);

        return true;
    }

    void TerrainPipeline::cleanup(EngineDevice& engineDevice, VkPipeline& pipeline) {
        vkDestroyPipeline(engineDevice.device(), pipeline, nullptr);
    }

}