#include "aveng_scene_loader.h"
#include "nlohmann/json.hpp"
#include "aveng_model.h"
#include "data.h"
#include <fstream>
#include <iostream>

using json = nlohmann::json;

namespace aveng {

    AvengSceneLoader::AvengSceneLoader(VkRenderData& _renderData) : renderData{ _renderData } {}
    AvengSceneLoader::~AvengSceneLoader() {}

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

    const std::vector<std::string>& AvengSceneLoader::getSceneTextures() const {
        const SceneData* scene = getCurrentSceneData();
        if (scene) {
            return scene->textures;
        }
        
        // Return empty vector if no current scene
        static std::vector<std::string> empty;
        return empty;
    }

    void AvengSceneLoader::createObjectsFromScene(const SceneData& scene, EngineDevice& engineDevice) {
        std::cout << "Creating objects for scene: " << scene.title << std::endl;
        
        for (const auto& objectData : scene.objects) {
            for (size_t i = 0; i < objectData.instances.size(); ++i) {
                const auto& instance = objectData.instances[i];
                
                // Validate texture index
                if (instance.textureId < 0 || static_cast<size_t>(instance.textureId) >= scene.textures.size()) {
                    throw std::runtime_error("Object texture ID " + std::to_string(instance.textureId) + 
                                           " is out of range. Scene has " + std::to_string(scene.textures.size()) + " textures.");
                }
                
                AvengAppObject object = AvengAppObject::createAppObject(instance.textureId);
                
                // Use shared model from cache for instanced rendering. Relies on model's pathname for lookup
                object.model = getOrCreateModel(objectData.path, engineDevice);
                
                // Set position
                object.transform.translation = instance.position;
                
                // currentSceneObjects.emplace(object.getId(), std::move(object));
                currentSceneObjects_v.emplace_back(std::move(object));
                
                //std::cout << "Created object from " << objectData.path 
                //         << " at position (" << instance.position.x 
                //         << ", " << instance.position.y 
                //         << ", " << instance.position.z << ")"
                //         << " with texture ID " << instance.textureId << std::endl;
            }
        }
    }

    void AvengSceneLoader::clearCurrentScene() {
        currentSceneObjects.clear();
        currentSceneId.clear();
        clearModelCache();
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
                    
                    // Parse data array (instances with position and texture)
                    if (objJson.contains("data") && objJson["data"].is_array()) {
                        for (const auto& instanceData : objJson["data"]) {
                            ObjectInstanceData instance;
                            
                            // Parse position
                            if (instanceData.contains("pos")) {
                                const auto& posJson = instanceData["pos"];
                                if (posJson.contains("x") && posJson.contains("y") && posJson.contains("z")) {
                                    instance.position = glm::vec3(
                                        posJson["x"].get<float>(),
                                        posJson["y"].get<float>(),
                                        posJson["z"].get<float>()
                                    );
                                }
                            }
                            
                            // Parse texture ID - STRICT VALIDATION. Assumes we always use external textures
                            if (!instanceData.contains("tex")) {
                                throw std::runtime_error("Object instance missing required 'tex' key in scene data");
                            }
                            
                            if (!instanceData["tex"].is_number_integer()) {
                                throw std::runtime_error("Object instance 'tex' key must be an integer value");
                            }
                            
                            instance.textureId = instanceData["tex"].get<int>();
                            
                            obj.instances.push_back(instance);
                        }
                    }
                    
                    scene.objects.push_back(obj);
                }
            }
            
            // Parse textures array
            if (sceneJson.contains("textures") && sceneJson["textures"].is_array()) {
                for (const auto& textureJson : sceneJson["textures"]) {
                    if (textureJson.is_string()) {
                        std::string texturePath = "textures/" + textureJson.get<std::string>();
                        scene.textures.push_back(texturePath);
                    }
                }
            }
            
            scenes[sceneId] = scene;
            std::cout << "Parsed scene: " << sceneId << " (" << scene.title << ") with " << scene.objects.size() << " object types" << std::endl;
        }
    }

    std::shared_ptr<AvengModel> AvengSceneLoader::getOrCreateModel(const std::string& filepath, EngineDevice& engineDevice) {
        // Check if model is already cached
        auto it = modelCache.find(filepath);
        if (it != modelCache.end()) {
            std::cout << "Using cached model for: " << filepath << std::endl;
            modelCountCache[filepath]++;
            return it->second;
        }
        
        // Create new model and cache it
        std::cout << "Loading new model: " << filepath << std::endl;
        auto model = AvengModel::createModelFromFile(engineDevice, renderData, filepath);

        // TODO - Bad code smells, maybe - shared_ptr
        auto sharedModel = std::shared_ptr<AvengModel>(model.release());

        modelCache[filepath] = sharedModel;
        modelCountCache[filepath] = 1;  // TMP: : this was just for debugging
        
        return sharedModel;
    }

    void AvengSceneLoader::clearModelCache() {
        std::cout << "Clearing model cache (" << modelCache.size() << " models)" << std::endl;
        modelCache.clear();
    }

}