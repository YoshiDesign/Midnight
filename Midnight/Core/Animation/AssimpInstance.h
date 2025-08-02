#pragma once

#include <memory>
#include <vector>
#include <glm/glm.hpp>

#include "AssimpModel.h"
#include "AssimpAnimClip.h"
#include "InstanceSettings.h"
#include "../data.h"

namespace aveng {

class AssimpInstance {
public:
    AssimpInstance(std::shared_ptr<AssimpModel> model, glm::vec3 position = glm::vec3(0.0f), 
                   glm::vec3 rotation = glm::vec3(0.0f), float modelScale = 1.0f);

    void updateAnimation(float deltaTime);
    void updateNodeTransformations();

    // Animation control
    void setAnimation(const std::string& animationName);
    void setAnimationByIndex(unsigned int index);
    std::shared_ptr<AssimpAnimClip> getCurrentAnimation() { return mCurrentAnimationClip; }

    // Getters
    std::shared_ptr<AssimpModel> getModel();
    glm::mat4 getInstanceRootMatrix() { return mInstanceRootMatrix; }
    InstanceSettings& getInstanceSettings() { return mInstanceSettings; }
    std::vector<NodeTransformData>& getNodeTransformData() { return mNodeTransformData; }
    const std::vector<glm::mat4>& getBoneTransformMatrices() { return mBoneTransformMatrices; }
    
    // Advanced debugging and validation
    bool validateAnimationData() const;
    std::string getAnimationReport() const;
    void debugAnimationState() const;
    
    // Geometry debugging controls
    static void setGeometryDebugLevel(int level) { sGeometryDebugLevel = level; }
    static int getGeometryDebugLevel() { return sGeometryDebugLevel; }

    // Setters
    void setPosition(const glm::vec3& position) { 
        mInstanceSettings.isWorldPosition = position; 
        updateModelRootMatrix(); 
    }
    void setRotation(const glm::vec3& rotation) { 
        mInstanceSettings.isWorldRotation = rotation; 
        updateModelRootMatrix(); 
    }
    void setScale(float scale) { 
        mInstanceSettings.isScale = scale; 
        updateModelRootMatrix(); 
    }

private:
    void updateModelRootMatrix();
    void optimizeAnimationCache();  // Cache optimization for animation performance
    
    // 🔧 FIXED: Hierarchical bone calculation methods
    void updateAnimationChannels(float wrappedTime);          // Phase 1: Update animated nodes using mNodeMap
    void calculateBoneTransformsHierarchical();               // Phase 2: Calculate bone matrices using mNodeList

    InstanceSettings mInstanceSettings{};
    std::vector<NodeTransformData> mNodeTransformData{};
    std::vector<glm::mat4> mBoneTransformMatrices{};
    
    // Static debug control
    static int sGeometryDebugLevel;
    
    /* transform matrices */
    glm::mat4 mInstanceRootMatrix = glm::mat4(1.0f);
    glm::mat4 mLocalTransformMatrix = glm::mat4(1.0f);
    glm::mat4 mLocalTranslationMatrix = glm::mat4(1.0f);
    glm::mat4 mLocalRotationMatrix = glm::mat4(1.0f);
    glm::mat4 mLocalScaleMatrix = glm::mat4(1.0f);
    glm::mat4 mLocalSwapAxisMatrix = glm::mat4(1.0f);
    glm::mat4 mModelRootMatrix = glm::mat4(1.0f);

    std::shared_ptr<AssimpModel> mAssimpModel = nullptr;
    std::shared_ptr<AssimpAnimClip> mCurrentAnimationClip = nullptr;
};

} // namespace aveng