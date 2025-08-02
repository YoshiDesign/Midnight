#include "AnimationManager.h"
#include "Logger.h"
#include <algorithm>
#include <chrono>

namespace aveng {

AnimationManager::AnimationManager() {
    // Setup callback functions using lambda expressions
    mModelInstData.miModelCheckCallbackFunction = [this](std::string modelPath) { 
        return checkModel(modelPath); 
    };
    mModelInstData.miModelAddCallbackFunction = [this](std::string modelPath) { 
        return addModel(modelPath); 
    };
    mModelInstData.miModelDeleteCallbackFunction = [this](std::string modelPath) { 
        removeModel(modelPath); 
    };
    
    mModelInstData.miInstanceAddCallbackFunction = [this](std::shared_ptr<AssimpModel> model) { 
        return addInstance(model); 
    };
    mModelInstData.miInstanceAddManyCallbackFunction = [this](std::shared_ptr<AssimpModel> model, int numInstances) { 
        addInstances(model, numInstances); 
    };
    mModelInstData.miInstanceDeleteCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance) { 
        removeInstance(instance); 
    };
    mModelInstData.miInstanceCloneCallbackFunction = [this](std::shared_ptr<AssimpInstance> instance) { 
        duplicateInstance(instance); 
    };
    
    Logger::log(1, "AnimationManager: Initialized with callback system\n");
}

AnimationManager::~AnimationManager() {
    // Clean up all instances and models
    mModelInstData.miAssimpInstances.clear();
    mModelInstData.miAssimpInstancesPerModel.clear();
    
    for (auto& model : mModelInstData.miModelList) {
        if (mCurrentRenderData) {
            model->cleanup(*mCurrentRenderData);
        }
    }
    mModelInstData.miModelList.clear();
    mModelCache.clear();
    
    Logger::log(1, "AnimationManager: Cleaned up all resources\n");
}

bool AnimationManager::loadModel(const std::string& modelPath, RenderData& renderData) {
    mCurrentRenderData = &renderData;
    
    if (hasModel(modelPath)) {
        Logger::log(1, "AnimationManager: Model '%s' already loaded\n", modelPath.c_str());
        return true;
    }
    
    auto model = std::make_shared<AssimpModel>();
    if (model->loadModel(renderData, modelPath)) {
        mModelCache[modelPath] = model;
        mModelInstData.miModelList.push_back(model);
        
        // Update render data
        renderData.rdLoadedModels++;
        if (model->hasAnimations()) {
            renderData.rdAnimatedModels++;
        }
        renderData.rdTotalBones += model->getBoneList().size();
        renderData.rdTotalNodes += model->getNodeList().size();
        renderData.rdTotalAnimationClips += model->getAnimClips().size();
        
        Logger::log(1, "AnimationManager: Successfully loaded model '%s' (%d bones, %d animations)\n", 
                   modelPath.c_str(), model->getBoneList().size(), model->getAnimClips().size());
        return true;
    }
    
    Logger::log(0, "AnimationManager: Failed to load model '%s'\n", modelPath.c_str());
    return false;
}

bool AnimationManager::hasModel(const std::string& modelPath) {
    return mModelCache.find(modelPath) != mModelCache.end();
}

void AnimationManager::deleteModel(const std::string& modelPath) {
    auto it = mModelCache.find(modelPath);
    if (it != mModelCache.end()) {
        // Mark for pending deletion (can't delete during command buffer execution)
        mModelInstData.miPendingDeleteAssimpModels.insert(it->second);
        
        // Remove from active list
        auto modelIt = std::find(mModelInstData.miModelList.begin(), 
                                mModelInstData.miModelList.end(), it->second);
        if (modelIt != mModelInstData.miModelList.end()) {
            mModelInstData.miModelList.erase(modelIt);
        }
        
        mModelCache.erase(it);
        Logger::log(1, "AnimationManager: Marked model '%s' for deletion\n", modelPath.c_str());
    }
}

std::shared_ptr<AssimpModel> AnimationManager::getModel(const std::string& modelPath) {
    auto it = mModelCache.find(modelPath);
    return (it != mModelCache.end()) ? it->second : nullptr;
}

std::shared_ptr<AssimpInstance> AnimationManager::createInstance(const std::string& modelPath, 
                                                                 glm::vec3 position, 
                                                                 glm::vec3 rotation, 
                                                                 float scale) {
    auto model = getModel(modelPath);
    if (!model) {
        Logger::log(0, "AnimationManager: Cannot create instance - model '%s' not loaded\n", modelPath.c_str());
        return nullptr;
    }
    
    auto instance = std::make_shared<AssimpInstance>(model, position, rotation, scale);
    mModelInstData.miAssimpInstances.push_back(instance);
    mModelInstData.miAssimpInstancesPerModel[modelPath].push_back(instance);
    
    if (mCurrentRenderData) {
        mCurrentRenderData->rdActiveInstances++;
    }
    
    Logger::log(1, "AnimationManager: Created instance of model '%s' at position (%.2f, %.2f, %.2f)\n", 
               modelPath.c_str(), position.x, position.y, position.z);
    return instance;
}

void AnimationManager::createInstances(const std::string& modelPath, int count) {
    for (int i = 0; i < count; ++i) {
        createInstance(modelPath);
    }
}

void AnimationManager::deleteInstance(std::shared_ptr<AssimpInstance> instance) {
    if (mModelInstData.miInstanceDeleteCallbackFunction) {
        mModelInstData.miInstanceDeleteCallbackFunction(instance);
    }
}

void AnimationManager::cloneInstance(std::shared_ptr<AssimpInstance> instance) {
    if (mModelInstData.miInstanceCloneCallbackFunction) {
        mModelInstData.miInstanceCloneCallbackFunction(instance);
    }
}

void AnimationManager::updateAnimations(float deltaTime) {
    if (!mCurrentRenderData) return;
    
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Update all instances
    for (auto& instance : mModelInstData.miAssimpInstances) {
        instance->updateAnimation(deltaTime);
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration<float, std::milli>(endTime - startTime);
    mCurrentRenderData->rdAnimationUpdateTime = duration.count();
}

void AnimationManager::resetRenderDataAnimationTotals(RenderData& renderData) {
    renderData.rdLoadedModels = mModelInstData.miModelList.size();
    renderData.rdActiveInstances = mModelInstData.miAssimpInstances.size();
    
    // Count animated models
    renderData.rdAnimatedModels = 0;
    renderData.rdTotalBones = 0;
    renderData.rdTotalNodes = 0;
    renderData.rdTotalAnimationClips = 0;
    
    // Specific to AssimpModel
    for (const auto& model : mModelInstData.miModelList) {
        if (model->hasAnimations()) {
            renderData.rdAnimatedModels++;
            renderData.rdTotalBones += model->getBoneList().size();
            renderData.rdTotalNodes += model->getNodeList().size();
            renderData.rdTotalAnimationClips += model->getAnimClips().size();
        }
    }
}

// Private callback implementations
bool AnimationManager::checkModel(const std::string& modelPath) {
    return hasModel(modelPath);
}

bool AnimationManager::addModel(const std::string& modelPath) {
    if (mCurrentRenderData) {
        return loadModel(modelPath, *mCurrentRenderData);
    }
    return false;
}

void AnimationManager::removeModel(const std::string& modelPath) {
    deleteModel(modelPath);
}

std::shared_ptr<AssimpInstance> AnimationManager::addInstance(std::shared_ptr<AssimpModel> model) {
    // Find model path from cache
    for (const auto& [path, cachedModel] : mModelCache) {
        if (cachedModel == model) {
            return createInstance(path);
        }
    }
    return nullptr;
}

void AnimationManager::addInstances(std::shared_ptr<AssimpModel> model, int numInstances) {
    for (const auto& [path, cachedModel] : mModelCache) {
        if (cachedModel == model) {
            createInstances(path, numInstances);
            return;
        }
    }
}

void AnimationManager::removeInstance(std::shared_ptr<AssimpInstance> instance) {
    auto it = std::find(mModelInstData.miAssimpInstances.begin(), 
                       mModelInstData.miAssimpInstances.end(), instance);
    if (it != mModelInstData.miAssimpInstances.end()) {
        mModelInstData.miAssimpInstances.erase(it);
        
        // Remove from per-model instances
        for (auto& [modelPath, instances] : mModelInstData.miAssimpInstancesPerModel) {
            auto modelIt = std::find(instances.begin(), instances.end(), instance);
            if (modelIt != instances.end()) {
                instances.erase(modelIt);
                break;
            }
        }
        
        if (mCurrentRenderData) {
            mCurrentRenderData->rdActiveInstances--;
        }
        
        Logger::log(1, "AnimationManager: Deleted instance\n");
    }
}

void AnimationManager::duplicateInstance(std::shared_ptr<AssimpInstance> instance) {
    if (!instance || !instance->getModel()) return;
    
    auto& settings = instance->getInstanceSettings();
    
    // Create clone with slightly offset position
    glm::vec3 offset(1.0f, 0.0f, 0.0f);
    auto clonedInstance = std::make_shared<AssimpInstance>(
        instance->getModel(),
        settings.isWorldPosition + offset,
        settings.isWorldRotation,
        settings.isScale
    );
    
    mModelInstData.miAssimpInstances.push_back(clonedInstance);
    
    // Find model path and add to per-model instances
    for (const auto& [path, model] : mModelCache) {
        if (model == instance->getModel()) {
            mModelInstData.miAssimpInstancesPerModel[path].push_back(clonedInstance);
            break;
        }
    }
    
    if (mCurrentRenderData) {
        mCurrentRenderData->rdActiveInstances++;
    }
    
    Logger::log(1, "AnimationManager: Cloned instance\n");
}

} // namespace aveng 