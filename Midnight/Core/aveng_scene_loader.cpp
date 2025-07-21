#include "aveng_scene_loader.h"
#include "nlohmann/json.hpp"
#include "aveng_model.h"
#include "data.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace aveng {

    AvengSceneLoader::AvengSceneLoader() = default;

    AvengSceneLoader::~AvengSceneLoader() = default;

    void AvengSceneLoader::load(const char* filepath, EngineDevice& engineDevice, const std::string& defaultSceneId) {
        std::ifstream file(filepath);
        if (!file.is_open()) {
            std::cerr << "Failed to open scene file: " << filepath << std::endl;
            return;
        }

        try {
            json jsonData;
            file >> jsonData;
            file.close();

            parseSceneData(jsonData);
            setCurrentScene(defaultSceneId, engineDevice);
            
            std::cout << "Loaded " << scenes.size() << " scenes from " << filepath << std::endl;
            std::cout << "Current scene: " << currentSceneId << " with " << getObjectCount() << " objects" << std::endl;
        }
        catch (const json::exception& e) {
            std::cerr << "JSON parsing error: " << e.what() << std::endl;
        }
    }

    bool AvengSceneLoader::setCurrentScene(const std::string& sceneId, EngineDevice& engineDevice) {
        auto it = scenes.find(sceneId);
        if (it == scenes.end()) {
            std::cerr << "Scene not found: " << sceneId << std::endl;
            return false;
        }

        clearCurrentScene();
        currentSceneId = sceneId;
        createObjectsFromScene(it->second, engineDevice);
        
        std::cout << "Switched to scene: " << sceneId << " (" << it->second.title << ")" << std::endl;
        return true;
    }

    const SceneData* AvengSceneLoader::getCurrentSceneData() const {
        auto it = scenes.find(currentSceneId);
        return it != scenes.end() ? &it->second : nullptr;
    }

    void AvengSceneLoader::createObjectsFromScene(const SceneData& scene, EngineDevice& engineDevice) {
        std::cout << "Creating objects for scene: " << scene.title << std::endl;
        
        for (const auto& objectData : scene.objects) {
            for (size_t i = 0; i < objectData.instances.size() && i < static_cast<size_t>(objectData.qty); ++i) {
                const auto& instance = objectData.instances[i];
                auto object = AvengAppObject::createAppObject(instance.textureId);
                
                // Load model with EngineDevice
                object.model = AvengModel::createModelFromFile(engineDevice, objectData.path);
                
                // Set position
                object.transform.translation = instance.position;
                
                currentSceneObjects.emplace(object.getId(), std::move(object));
                
                std::cout << "Created object from " << objectData.path 
                         << " at position (" << instance.position.x 
                         << ", " << instance.position.y 
                         << ", " << instance.position.z << ")"
                         << " with texture ID " << instance.textureId << std::endl;
            }
        }
    }

    void AvengSceneLoader::clearCurrentScene() {
        currentSceneObjects.clear();
        currentSceneId.clear();
    }

    void AvengSceneLoader::parseSceneData(const json& jsonData) {
        for (const auto& [sceneId, sceneJson] : jsonData.items()) {
            SceneData scene;
            
            // Parse title
            if (sceneJson.contains("title")) {
                scene.title = sceneJson["title"].get<std::string>();
            }
            
            // Parse objects array
            if (sceneJson.contains("objects") && sceneJson["objects"].is_array()) {
                for (const auto& objJson : sceneJson["objects"]) {
                    ObjectData obj;
                    
                    // Parse path
                    if (objJson.contains("path")) {
                        obj.path = objJson["path"].get<std::string>();
                    }
                    
                    // Parse qty
                    if (objJson.contains("qty")) {
                        obj.qty = objJson["qty"].get<int>();
                    }
                    
                    // Parse data array (instances with position and texture)
                    if (objJson.contains("data") && objJson["data"].is_array()) {
                        for (const auto& instanceJson : objJson["data"]) {
                            ObjectInstanceData instance;
                            
                            // Parse position
                            if (instanceJson.contains("pos")) {
                                const auto& posJson = instanceJson["pos"];
                                if (posJson.contains("x") && posJson.contains("y") && posJson.contains("z")) {
                                    instance.position = glm::vec3(
                                        posJson["x"].get<float>(),
                                        posJson["y"].get<float>(),
                                        posJson["z"].get<float>()
                                    );
                                }
                            }
                            
                            // Parse texture ID
                            if (instanceJson.contains("tex")) {
                                instance.textureId = instanceJson["tex"].get<int>();
                            }
                            
                            obj.instances.push_back(instance);
                        }
                    }
                    
                    scene.objects.push_back(obj);
                }
            }
            
            scenes[sceneId] = scene;
            std::cout << "Parsed scene: " << sceneId << " (" << scene.title << ") with " << scene.objects.size() << " object types" << std::endl;
        }
    }

}