#include "PipelineConfigManager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "../data.h"  // For TransformedVertex
#include "../aveng_model.h"  // For AvengModel::Vertex

namespace aveng {

    PipelineConfigManager::PipelineConfigManager(EngineDevice& device) : engineDevice(device) {}

    void PipelineConfigManager::loadPipelineConfig(const std::string& configPath) {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open pipeline config file: " + configPath);
        }

        nlohmann::json json;
        file >> json;

        definitions.clear();

        for (const auto& pipelineJson : json["pipelines"]) {
            PipelineDefinition def;
            def.name = pipelineJson["name"];
            def.id = pipelineJson["id"];
            def.vertexShader = pipelineJson["vertexShader"];
            def.fragmentShader = pipelineJson["fragmentShader"];

            // Parse rasterization settings
            if (pipelineJson.contains("rasterization")) {
                const auto& raster = pipelineJson["rasterization"];
                if (raster.contains("polygonMode")) {
                    def.polygonMode = parsePolygonMode(raster["polygonMode"]);
                }
                if (raster.contains("cullMode")) {
                    def.cullMode = parseCullMode(raster["cullMode"]);
                }
                if (raster.contains("frontFace")) {
                    def.frontFace = parseFrontFace(raster["frontFace"]);
                }
            }

            // Parse blending settings
            if (pipelineJson.contains("blending")) {
                const auto& blend = pipelineJson["blending"];
                def.blendingEnabled = blend.value("enabled", false);
                if (def.blendingEnabled) {
                    def.srcColorBlend = parseBlendFactor(blend.value("srcColorBlend", "ONE"));
                    def.dstColorBlend = parseBlendFactor(blend.value("dstColorBlend", "ZERO"));
                    def.colorBlendOp = parseBlendOp(blend.value("colorBlendOp", "ADD"));
                }
            }

            // Parse specialization constants
            if (pipelineJson.contains("specialization")) {
                const auto& spec = pipelineJson["specialization"];
                def.useTextureArraySize = spec.value("textureArraySize", false);
            }

            definitions.push_back(def);
            std::cout << "Loaded pipeline definition: " << def.name << " (ID: " << def.id << ")" << std::endl;
        }

        std::cout << "Loaded " << definitions.size() << " pipeline definitions" << std::endl;
    }

    void PipelineConfigManager::createPipelines(VkRenderPass renderPass, VkPipelineLayout pipelineLayout, uint32_t textureArraySize) {
        std::cout << "Creating " << definitions.size() << " pipelines from configuration..." << std::endl;

        for (const auto& def : definitions) {
            createPipelineFromDefinition(def, renderPass, pipelineLayout, textureArraySize);
        }

        std::cout << "Successfully created all configured pipelines" << std::endl;
    }

    void PipelineConfigManager::createPipelineFromDefinition(const PipelineDefinition& def, VkRenderPass renderPass, 
                                                           VkPipelineLayout pipelineLayout, uint32_t textureArraySize) {
        PipelineConfig config{};
        GFXPipeline::defaultPipelineConfig(config);
        config.renderPass = renderPass;
        config.pipelineLayout = pipelineLayout;
        
        // Set vertex input layout based on pipeline type
        if (def.name == "ANIMATED") {
            // DEBUG: Manually create AnimatedVertex layout for raw geometry testing
            //config.bindingDescriptions.resize(1);
            //config.bindingDescriptions[0].binding = 0;
            //config.bindingDescriptions[0].stride = sizeof(AnimatedVertex);
            //config.bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            //
            //config.attributeDescriptions.clear();
            //// FIXED: All vec4 layout - perfect alignment, no padding
            //config.attributeDescriptions.push_back({0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(AnimatedVertex, position)});   // vec4: pos.xyz + texCoord.x
            //config.attributeDescriptions.push_back({1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(AnimatedVertex, color)});      // vec4: color.xyz + texCoord.y
            //config.attributeDescriptions.push_back({2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(AnimatedVertex, normal)});     // vec4: normal.xyz + unused.w
            //// Location 3 removed - texCoord now packed in position.w and color.w
            //config.attributeDescriptions.push_back({4, 0, VK_FORMAT_R32G32B32A32_SINT, offsetof(AnimatedVertex, boneIds)});      // ivec4: unchanged
            //config.attributeDescriptions.push_back({5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(AnimatedVertex, boneWeights)});  // vec4: unchanged
            //
            //std::cout << "Pipeline '" << def.name << "': Using AnimatedVertex layout (" 
            //          << config.attributeDescriptions.size() << " attributes) - DEBUG MODE" << std::endl;
        } else if (def.name.find("INSTANCED") != std::string::npos) {
            // Use instanced vertex layout for instanced rendering
            config.bindingDescriptions = AvengModel::getInstancedBindingDescriptions();
            config.attributeDescriptions = AvengModel::getInstancedAttributeDescriptions();
            std::cout << "Pipeline '" << def.name << "': Using Instanced layout (" 
                      << config.attributeDescriptions.size() << " attributes)" << std::endl;
        } else {
            // Use AvengModel::Vertex layout for static models  
            config.bindingDescriptions = AvengModel::Vertex::getBindingDescriptions();
            config.attributeDescriptions = AvengModel::Vertex::getAttributeDescriptions();
            std::cout << "Pipeline '" << def.name << "': Using AvengModel::Vertex layout (" 
                      << config.attributeDescriptions.size() << " attributes)" << std::endl;
        }

        // Apply rasterization settings
        config.rasterizationInfo.polygonMode = def.polygonMode;
        config.rasterizationInfo.cullMode = def.cullMode;
        config.rasterizationInfo.frontFace = def.frontFace;

        // Apply blending settings
        if (def.blendingEnabled) {
            config.colorBlendAttachment.blendEnable = VK_TRUE;
            config.colorBlendAttachment.srcColorBlendFactor = def.srcColorBlend;
            config.colorBlendAttachment.dstColorBlendFactor = def.dstColorBlend;
            config.colorBlendAttachment.colorBlendOp = def.colorBlendOp;
        }

        // Setup specialization constants if needed
        VkSpecializationMapEntry specEntry{};
        VkSpecializationInfo specInfo{};
        if (def.useTextureArraySize) {
            specEntry.constantID = 0;
            specEntry.offset = 0;
            specEntry.size = sizeof(uint32_t);

            specInfo.mapEntryCount = 1;
            specInfo.pMapEntries = &specEntry;
            specInfo.dataSize = sizeof(uint32_t);
            specInfo.pData = &textureArraySize;

            config.fragmentSpecializationInfo = &specInfo;
        }

        // Create the pipeline with error handling
        try {
            auto pipeline = std::make_unique<GFXPipeline>(
                engineDevice,
                def.vertexShader,
                def.fragmentShader,
                config
            );

            // Store by both name and ID
            GFXPipeline* pipelinePtr = pipeline.get();  // Get raw pointer before moving
            pipelinesByName[def.name] = std::move(pipeline);
            pipelinesById[def.id] = pipelinePtr;

            std::cout << "Created pipeline: " << def.name << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "FAILED to create pipeline '" << def.name << "' (ID: " << def.id << "): " << e.what() << std::endl;
            std::cerr << "  Vertex shader: " << def.vertexShader << std::endl;
            std::cerr << "  Fragment shader: " << def.fragmentShader << std::endl;
        }
    }

    GFXPipeline* PipelineConfigManager::getPipeline(const std::string& name) {
        auto it = pipelinesByName.find(name);
        return (it != pipelinesByName.end()) ? it->second.get() : nullptr;
    }

    GFXPipeline* PipelineConfigManager::getPipeline(int id) {
        auto it = pipelinesById.find(id);
        return (it != pipelinesById.end()) ? it->second : nullptr;
    }

    bool PipelineConfigManager::hasPipeline(const std::string& name) const {
        return pipelinesByName.find(name) != pipelinesByName.end();
    }

    bool PipelineConfigManager::hasPipeline(int id) const {
        return pipelinesById.find(id) != pipelinesById.end();
    }

    std::vector<std::string> PipelineConfigManager::getPipelineNames() const {
        std::vector<std::string> names;
        for (const auto& pair : pipelinesByName) {
            names.push_back(pair.first);
        }
        return names;
    }

    // Helper parsing methods
    VkPolygonMode PipelineConfigManager::parsePolygonMode(const std::string& mode) {
        if (mode == "FILL") return VK_POLYGON_MODE_FILL;
        if (mode == "LINE") return VK_POLYGON_MODE_LINE;
        if (mode == "POINT") return VK_POLYGON_MODE_POINT;
        throw std::runtime_error("Unknown polygon mode: " + mode);
    }

    VkCullModeFlags PipelineConfigManager::parseCullMode(const std::string& mode) {
        if (mode == "NONE") return VK_CULL_MODE_NONE;
        if (mode == "FRONT") return VK_CULL_MODE_FRONT_BIT;
        if (mode == "BACK") return VK_CULL_MODE_BACK_BIT;
        if (mode == "FRONT_AND_BACK") return VK_CULL_MODE_FRONT_AND_BACK;
        throw std::runtime_error("Unknown cull mode: " + mode);
    }

    VkFrontFace PipelineConfigManager::parseFrontFace(const std::string& face) {
        if (face == "CLOCKWISE") return VK_FRONT_FACE_CLOCKWISE;
        if (face == "COUNTER_CLOCKWISE") return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        throw std::runtime_error("Unknown front face: " + face);
    }

    VkBlendFactor PipelineConfigManager::parseBlendFactor(const std::string& factor) {
        if (factor == "ZERO") return VK_BLEND_FACTOR_ZERO;
        if (factor == "ONE") return VK_BLEND_FACTOR_ONE;
        if (factor == "SRC_COLOR") return VK_BLEND_FACTOR_SRC_COLOR;
        if (factor == "ONE_MINUS_SRC_COLOR") return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        if (factor == "DST_COLOR") return VK_BLEND_FACTOR_DST_COLOR;
        if (factor == "ONE_MINUS_DST_COLOR") return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        if (factor == "SRC_ALPHA") return VK_BLEND_FACTOR_SRC_ALPHA;
        if (factor == "ONE_MINUS_SRC_ALPHA") return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        if (factor == "DST_ALPHA") return VK_BLEND_FACTOR_DST_ALPHA;
        if (factor == "ONE_MINUS_DST_ALPHA") return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        throw std::runtime_error("Unknown blend factor: " + factor);
    }

    VkBlendOp PipelineConfigManager::parseBlendOp(const std::string& op) {
        if (op == "ADD") return VK_BLEND_OP_ADD;
        if (op == "SUBTRACT") return VK_BLEND_OP_SUBTRACT;
        if (op == "REVERSE_SUBTRACT") return VK_BLEND_OP_REVERSE_SUBTRACT;
        if (op == "MIN") return VK_BLEND_OP_MIN;
        if (op == "MAX") return VK_BLEND_OP_MAX;
        throw std::runtime_error("Unknown blend operation: " + op);
    }

} 