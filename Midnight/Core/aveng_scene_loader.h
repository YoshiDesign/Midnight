#pragma once
#include <string>
#include <unordered_map>
#include <vector>
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
        int qty;
        std::vector<ObjectInstanceData> instances;
    };

    // Structure to represent a scene from JSON
    struct SceneData {
        std::string title;
        std::vector<ObjectData> objects;
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
        const AvengAppObject::Map& getAppObjects() const { return currentSceneObjects; }
        
        /**
         * Get current scene info
         */
        const std::string& getCurrentSceneId() const { return currentSceneId; }
        const SceneData* getCurrentSceneData() const;
        
        /**
         * Get total number of objects in current scene
         */
        size_t getObjectCount() const { return currentSceneObjects.size(); }

    private:
        void createObjectsFromScene(const SceneData& scene, EngineDevice& engineDevice);
        void clearCurrentScene();
        
        // JSON parsing
        void parseSceneData(const nlohmann::json& jsonData);
        
        std::string currentSceneId;
        std::unordered_map<std::string, SceneData> scenes;
        AvengAppObject::Map currentSceneObjects;
    };
}