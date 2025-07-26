#include "AssimpAnimChannel.h"
#include "Tools.h"
#include <glm/gtc/matrix_transform.hpp>

namespace aveng {

void AssimpAnimChannel::loadChannelData(aiNodeAnim* nodeAnim) {
    mTargetNodeName = nodeAnim->mNodeName.C_Str();
    
    // Load all keyframes and combine position, rotation, scale data
    unsigned int maxKeys = std::max({nodeAnim->mNumPositionKeys, 
                                    nodeAnim->mNumRotationKeys, 
                                    nodeAnim->mNumScalingKeys});
    
    mKeyFrames.reserve(maxKeys);
    
    // Process all keyframes - we'll interpolate between them as needed
    for (unsigned int i = 0; i < maxKeys; ++i) {
        AnimationKeyFrame keyFrame;
        
        // Get position (use last available if we run out)
        if (i < nodeAnim->mNumPositionKeys) {
            keyFrame.timeStamp = nodeAnim->mPositionKeys[i].mTime;
            keyFrame.position = Tools::convertAiToGLM(nodeAnim->mPositionKeys[i].mValue);
        } else if (nodeAnim->mNumPositionKeys > 0) {
            keyFrame.position = Tools::convertAiToGLM(nodeAnim->mPositionKeys[nodeAnim->mNumPositionKeys - 1].mValue);
        }
        
        // Get rotation
        if (i < nodeAnim->mNumRotationKeys) {
            keyFrame.timeStamp = nodeAnim->mRotationKeys[i].mTime;
            keyFrame.rotation = Tools::convertAiToGLM(nodeAnim->mRotationKeys[i].mValue);
        } else if (nodeAnim->mNumRotationKeys > 0) {
            keyFrame.rotation = Tools::convertAiToGLM(nodeAnim->mRotationKeys[nodeAnim->mNumRotationKeys - 1].mValue);
        }
        
        // Get scale
        if (i < nodeAnim->mNumScalingKeys) {
            keyFrame.timeStamp = nodeAnim->mScalingKeys[i].mTime;
            keyFrame.scale = Tools::convertAiToGLM(nodeAnim->mScalingKeys[i].mValue, false); // Scale doesn't need coord conversion
        } else if (nodeAnim->mNumScalingKeys > 0) {
            keyFrame.scale = Tools::convertAiToGLM(nodeAnim->mScalingKeys[nodeAnim->mNumScalingKeys - 1].mValue, false);
        } else {
            keyFrame.scale = glm::vec3(1.0f); // Default scale
        }
        
        mKeyFrames.push_back(keyFrame);
    }
}

std::string AssimpAnimChannel::getTargetNodeName() {
    return mTargetNodeName;
}

int AssimpAnimChannel::getBoneId() {
    return mBoneId;
}

void AssimpAnimChannel::setBoneId(unsigned int id) {
    mBoneId = id;
}

glm::mat4 AssimpAnimChannel::getTransformationMatrix(float animationTime) {
    glm::vec3 position = getInterpolatedPosition(animationTime);
    glm::quat rotation = getInterpolatedRotation(animationTime);
    glm::vec3 scale = getInterpolatedScale(animationTime);
    
    glm::mat4 translationMatrix = glm::translate(glm::mat4(1.0f), position);
    glm::mat4 rotationMatrix = glm::mat4_cast(rotation);
    glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
    
    return translationMatrix * rotationMatrix * scaleMatrix;
}

glm::vec3 AssimpAnimChannel::getInterpolatedPosition(float animationTime) {
    if (mKeyFrames.size() == 1 || animationTime <= mKeyFrames[0].timeStamp) {
        return mKeyFrames[0].position;
    }
    
    if (animationTime >= mKeyFrames.back().timeStamp) {
        return mKeyFrames.back().position;
    }
    
    // Find the two keyframes to interpolate between
    for (size_t i = 0; i < mKeyFrames.size() - 1; ++i) {
        if (animationTime >= mKeyFrames[i].timeStamp && animationTime <= mKeyFrames[i + 1].timeStamp) {
            float factor = getScaleFactor(mKeyFrames[i].timeStamp, mKeyFrames[i + 1].timeStamp, animationTime);
            return glm::mix(mKeyFrames[i].position, mKeyFrames[i + 1].position, factor);
        }
    }
    
    return mKeyFrames[0].position; // Fallback
}

glm::quat AssimpAnimChannel::getInterpolatedRotation(float animationTime) {
    if (mKeyFrames.size() == 1 || animationTime <= mKeyFrames[0].timeStamp) {
        return mKeyFrames[0].rotation;
    }
    
    if (animationTime >= mKeyFrames.back().timeStamp) {
        return mKeyFrames.back().rotation;
    }
    
    // Find the two keyframes to interpolate between
    for (size_t i = 0; i < mKeyFrames.size() - 1; ++i) {
        if (animationTime >= mKeyFrames[i].timeStamp && animationTime <= mKeyFrames[i + 1].timeStamp) {
            float factor = getScaleFactor(mKeyFrames[i].timeStamp, mKeyFrames[i + 1].timeStamp, animationTime);
            return glm::slerp(mKeyFrames[i].rotation, mKeyFrames[i + 1].rotation, factor);
        }
    }
    
    return mKeyFrames[0].rotation; // Fallback
}

glm::vec3 AssimpAnimChannel::getInterpolatedScale(float animationTime) {
    if (mKeyFrames.size() == 1 || animationTime <= mKeyFrames[0].timeStamp) {
        return mKeyFrames[0].scale;
    }
    
    if (animationTime >= mKeyFrames.back().timeStamp) {
        return mKeyFrames.back().scale;
    }
    
    // Find the two keyframes to interpolate between
    for (size_t i = 0; i < mKeyFrames.size() - 1; ++i) {
        if (animationTime >= mKeyFrames[i].timeStamp && animationTime <= mKeyFrames[i + 1].timeStamp) {
            float factor = getScaleFactor(mKeyFrames[i].timeStamp, mKeyFrames[i + 1].timeStamp, animationTime);
            return glm::mix(mKeyFrames[i].scale, mKeyFrames[i + 1].scale, factor);
        }
    }
    
    return mKeyFrames[0].scale; // Fallback
}

float AssimpAnimChannel::getScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime) {
    float midWayLength = animationTime - lastTimeStamp;
    float framesDiff = nextTimeStamp - lastTimeStamp;
    return midWayLength / framesDiff;
}

} // namespace aveng 