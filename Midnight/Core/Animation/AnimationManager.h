#pragma once

#include "ModelAndInstanceData.h"
#include "AssimpModel.h"
#include "AssimpInstance.h"
#include "../data.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace aveng {

/**
 * Animation Manager that handles loading models and creating instances
 * Uses the callback pattern from ModelAndInstanceData.h
 */
class AnimationManager {
public:
    AnimationManager();
    ~AnimationManager();

    // Model management
    bool loadModel(const std::string& modelPath, RenderData& renderData);
    bool hasModel(const std::string& modelPath);
    void deleteModel(const std::string& modelPath);
    std::shared_ptr<AssimpModel> getModel(const std::string& modelPath);
    
    // Instance management  
    std::shared_ptr<AssimpInstance> createInstance(const std::string& modelPath, 
                                                   glm::vec3 position = glm::vec3(0.0f),
                                                   glm::vec3 rotation = glm::vec3(0.0f), 
                                                   float scale = 1.0f);
    void createInstances(const std::string& modelPath, int count);
    void deleteInstance(std::shared_ptr<AssimpInstance> instance);
    void cloneInstance(std::shared_ptr<AssimpInstance> instance);
    
    // Animation updates
    void updateAnimations(float deltaTime);
    
    // Debug information
    void resetRenderDataAnimationTotals(RenderData& renderData);
    
    // Getters
    const std::vector<std::shared_ptr<AssimpModel>>& getModels() const { return mModelInstData.miModelList; }
    const std::vector<std::shared_ptr<AssimpInstance>>& getInstances() const { return mModelInstData.miAssimpInstances; }
    size_t getModelCount() const { return mModelInstData.miModelList.size(); }
    size_t getInstanceCount() const { return mModelInstData.miAssimpInstances.size(); }

private:
    // Callback implementations
    bool checkModel(const std::string& modelPath);
    bool addModel(const std::string& modelPath);
    void removeModel(const std::string& modelPath);
    
    std::shared_ptr<AssimpInstance> addInstance(std::shared_ptr<AssimpModel> model);
    void addInstances(std::shared_ptr<AssimpModel> model, int numInstances);
    void removeInstance(std::shared_ptr<AssimpInstance> instance);
    void duplicateInstance(std::shared_ptr<AssimpInstance> instance);
    
    ModelAndInstanceData mModelInstData;
    std::unordered_map<std::string, std::shared_ptr<AssimpModel>> mModelCache;
    RenderData* mCurrentRenderData = nullptr;
};

} // namespace aveng 