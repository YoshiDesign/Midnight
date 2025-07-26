#include "AssimpInstance.h"
#include "Logger.h"
#include "Tools.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace aveng {

AssimpInstance::AssimpInstance(std::shared_ptr<AssimpModel> model, glm::vec3 position, glm::vec3 rotation, float modelScale) 
    : mAssimpModel(model) {
    if (!model) {
        Logger::log(0, "AssimpInstance: Cannot create instance with null model\n");
        return;
    }

    // Initialize instance settings
    mInstanceSettings.isWorldPosition = position;
    mInstanceSettings.isWorldRotation = rotation;
    mInstanceSettings.isScale = modelScale;

    // Initialize animation data
    const auto& boneList = mAssimpModel->getBoneList();
    mNodeTransformData.resize(boneList.size());
    mBoneTransformMatrices.resize(boneList.size(), glm::mat4(1.0f));

    // Save model root matrix
    mModelRootMatrix = mAssimpModel->getRootTransformationMatrix();

    // Initialize current animation state
    if (mAssimpModel->hasAnimations() && !mAssimpModel->getAnimClips().empty()) {
        mCurrentAnimationClip = mAssimpModel->getAnimClips()[0];
        Logger::log(1, "AssimpInstance: Initialized with animation '%s'\n", 
                   mCurrentAnimationClip->getClipName().c_str());
    }

    updateModelRootMatrix();
    
    Logger::log(1, "AssimpInstance: Created instance for model '%s' with %d bones\n", 
               mAssimpModel->getModelFileName().c_str(), boneList.size());
}

void AssimpInstance::updateModelRootMatrix() {
    // Build transformation matrices
    mLocalScaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(mInstanceSettings.isScale));

    // Handle coordinate system conversion if needed
    if (mInstanceSettings.isSwapYZAxis) {
        mLocalSwapAxisMatrix = Tools::getCoordinateConversionMatrix();
    } else {
        mLocalSwapAxisMatrix = glm::mat4(1.0f);
    }

    // Rotation
    mLocalRotationMatrix = glm::mat4_cast(glm::quat(glm::radians(mInstanceSettings.isWorldRotation)));

    // Translation  
    mLocalTranslationMatrix = glm::translate(glm::mat4(1.0f), mInstanceSettings.isWorldPosition);

    // Combine transformations
    mLocalTransformMatrix = mLocalTranslationMatrix * mLocalRotationMatrix * mLocalSwapAxisMatrix * mLocalScaleMatrix;
    mInstanceRootMatrix = mLocalTransformMatrix * mModelRootMatrix;
}

void AssimpInstance::updateAnimation(float deltaTime) {
    if (!mAssimpModel || !mCurrentAnimationClip) {
        return;
    }

    // Update animation time
    mInstanceSettings.isAnimPlayTimePos += deltaTime * mInstanceSettings.isAnimSpeedFactor;
    
    // Get current pose from animation clip
    mCurrentAnimationClip->getBoneTransformations(mInstanceSettings.isAnimPlayTimePos, 
                                                  mBoneTransformMatrices,
                                                  mAssimpModel->getBoneList());

    // Update root node transformation matrix
    updateModelRootMatrix();
    
    // Update all nodes in the hierarchy
    updateNodeTransformations();
}

void AssimpInstance::updateNodeTransformations() {
    const auto& nodeList = mAssimpModel->getNodeList();
    
    // Update all nodes with current animation state
    for (auto& node : nodeList) {
        node->setRootTransformMatrix(mInstanceRootMatrix);
        node->updateTRSMatrix();
    }
}

void AssimpInstance::setAnimation(const std::string& animationName) {
    const auto& animClips = mAssimpModel->getAnimClips();
    
    for (const auto& clip : animClips) {
        if (clip->getClipName() == animationName) {
            mCurrentAnimationClip = clip;
            mInstanceSettings.isAnimPlayTimePos = 0.0f; // Reset animation time
            Logger::log(1, "AssimpInstance: Switched to animation '%s'\n", animationName.c_str());
            return;
        }
    }
    
    Logger::log(0, "AssimpInstance: Animation '%s' not found\n", animationName.c_str());
}

void AssimpInstance::setAnimationByIndex(unsigned int index) {
    const auto& animClips = mAssimpModel->getAnimClips();
    
    if (index < animClips.size()) {
        mCurrentAnimationClip = animClips[index];
        mInstanceSettings.isAnimPlayTimePos = 0.0f;
        Logger::log(1, "AssimpInstance: Switched to animation %d ('%s')\n", 
                   index, mCurrentAnimationClip->getClipName().c_str());
    } else {
        Logger::log(0, "AssimpInstance: Animation index %d out of range\n", index);
    }
}

std::shared_ptr<AssimpModel> AssimpInstance::getModel() {
    return mAssimpModel;
}
}