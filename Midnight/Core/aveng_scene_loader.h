#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include "app_object.h"
#include "../CoreVK/EngineDevice.h"
#include "nlohmann/json.hpp"

namespace aveng {
    
    // Structure to represent individual object instance data
    struct ObjectInstanceData {
        glm::vec3 position;
        int textureId = 0;
    };

    // Structure to represent object data from JSON
    struct ObjectData {
        std::string path;
        std::vector<ObjectInstanceData> instances;
    };

    // Structure to represent a scene from JSON
    struct SceneData {
        std::string title;
        std::vector<ObjectData> objects;
        std::vector<std::string> textures;  // Scene-specific texture list
    };

    class AvengSceneLoader {
    public:
        AvengSceneLoader();
        ~AvengSceneLoader();

        /**
         * Load scenes from JSON file and set current scene
         */
        void load(const char* filepath, EngineDevice& engineDevice, const std::string& defaultSceneId = "scene-id-1");
        
        /**
         * Switch to a different scene by ID
         */
        bool setCurrentScene(const std::string& sceneId, EngineDevice& engineDevice);
        
        /**
         * Get the current scene's app objects for rendering
         */
        const std::vector<AvengAppObject>& getAppObjects() const { return currentSceneObjects_v; }
        
        const std::unordered_map<std::string, int> getModelCountCache() const { return modelCountCache; }\
            std::unordered_map<std::string, std::shared_ptr<AvengModel>> getModelCache() const { return modelCache; }
        
        /**
         * Get current scene info
         */
        const std::string& getCurrentSceneId() const { return currentSceneId; }
        const SceneData* getCurrentSceneData() const;
        
        /**
         * Get total number of objects in current scene
         */
        size_t getObjectCount() const { return currentSceneObjects_v.size(); }
        
        /**
         * Get texture paths from current scene
         */
        const std::vector<std::string>& getSceneTextures() const;

    private:
        void createObjectsFromScene(const SceneData& scene, EngineDevice& engineDevice);
        void clearCurrentScene();
        
        // JSON parsing
        void parseSceneData(const nlohmann::json& jsonData);
        
        // Model caching for instanced rendering
        std::shared_ptr<AvengModel> getOrCreateModel(const std::string& modelPath, EngineDevice& engineDevice);
        void clearModelCache();
        
        std::string currentSceneId;
        std::unordered_map<std::string, SceneData> scenes;
        AvengAppObject::Map currentSceneObjects;
        std::vector<AvengAppObject> currentSceneObjects_v;
        
        // Model cache - shared models by file path
        std::unordered_map<std::string, std::shared_ptr<AvengModel>> modelCache;
        std::unordered_map<std::string, int> modelCountCache;
    };
}