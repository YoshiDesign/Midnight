#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "../../CoreVK/EngineDevice.h"
#include "../../CoreVK/GFXPipeline.h"
#include "nlohmann/json.hpp"

namespace aveng {

    struct PipelineDefinition {
        std::string name;
        int id;
        std::string vertexShader;
        std::string fragmentShader;
        
        // Rasterization settings
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        
        // Blending settings
        bool blendingEnabled = false;
        VkBlendFactor srcColorBlend = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstColorBlend = VK_BLEND_FACTOR_ZERO;
        VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
        
        // Specialization constants
        bool useTextureArraySize = false;
    };

    class PipelineConfigManager {
    public:
        PipelineConfigManager(EngineDevice& device);
        
        // Load pipeline definitions from JSON file
        void loadPipelineConfig(const std::string& configPath);
        
        // Create all pipelines from loaded definitions
        void createPipelines(VkRenderPass renderPass, VkPipelineLayout pipelineLayout, uint32_t textureArraySize);
        
        // Get pipeline by name or ID
        GFXPipeline* getPipeline(const std::string& name);
        GFXPipeline* getPipeline(int id);
        
        // Get all pipeline names
        std::vector<std::string> getPipelineNames() const;
        
        // Check if pipeline exists
        bool hasPipeline(const std::string& name) const;
        bool hasPipeline(int id) const;
        
    private:
        EngineDevice& engineDevice;
        std::vector<PipelineDefinition> definitions;
        std::map<std::string, std::unique_ptr<GFXPipeline>> pipelinesByName;
        std::map<int, GFXPipeline*> pipelinesById;  // Raw pointers for ID-based lookup
        
        // Helper methods
        VkPolygonMode parsePolygonMode(const std::string& mode);
        VkCullModeFlags parseCullMode(const std::string& mode);
        VkFrontFace parseFrontFace(const std::string& face);
        VkBlendFactor parseBlendFactor(const std::string& factor);
        VkBlendOp parseBlendOp(const std::string& op);
        
        void createPipelineFromDefinition(const PipelineDefinition& def, VkRenderPass renderPass, 
                                        VkPipelineLayout pipelineLayout, uint32_t textureArraySize);

        void createComputePipeline();
    };

} 