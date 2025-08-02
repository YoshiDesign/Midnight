#include "AssimpAnimChannel.h"
#include "Tools.h"
#include <glm/gtc/matrix_transform.hpp>
#include <chrono>

namespace aveng {

void AssimpAnimChannel::loadChannelData(aiNodeAnim* nodeAnim) {
    mTargetNodeName = nodeAnim->mNodeName.C_Str();
    
    // Load pre/post animation behavior (like reference implementation)
    mPreState = nodeAnim->mPreState;
    mPostState = nodeAnim->mPostState;
    
    Logger::log(2, "AssimpAnimChannel: Loading '%s' with preState %u, postState %u\n", 
               mTargetNodeName.c_str(), mPreState, mPostState);
    
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
    
    // Precalculate inverse time differences for performance (like reference implementation)
    mInverseTimeDiffs.reserve(mKeyFrames.size() - 1);
    for (size_t i = 0; i < mKeyFrames.size() - 1; ++i) {
        float timeDiff = static_cast<float>(mKeyFrames[i + 1].timeStamp - mKeyFrames[i].timeStamp);
        if (timeDiff > 0.0f) {
            mInverseTimeDiffs.push_back(1.0f / timeDiff);
        } else {
            mInverseTimeDiffs.push_back(0.0f); // Avoid division by zero
        }
    }
    
    // Optimize memory layout for better cache performance
    optimizeMemoryLayout();
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
    if (mKeyFrames.empty()) {
        return glm::vec3(0.0f); // No keyframes
    }
    
    if (mKeyFrames.size() == 1) {
        return mKeyFrames[0].position;
    }
    
    // Handle time before first keyframe (pre-animation behavior)
    if (animationTime < static_cast<float>(mKeyFrames[0].timeStamp)) {
        switch (static_cast<AnimBehavior>(mPreState)) {
            case AnimBehavior::DEFAULT:
                return glm::vec3(0.0f); // Don't change vertex position
            case AnimBehavior::CONSTANT:
                return mKeyFrames[0].position; // Use first keyframe value
            default:
                Logger::log(1, "AssimpAnimChannel: preState %u not implemented for position\n", mPreState);
                return mKeyFrames[0].position;
        }
    }
    
    // Handle time after last keyframe (post-animation behavior)
    if (animationTime >= static_cast<float>(mKeyFrames.back().timeStamp)) {
        switch (static_cast<AnimBehavior>(mPostState)) {
            case AnimBehavior::DEFAULT:
                return glm::vec3(0.0f); // Don't change vertex position
            case AnimBehavior::CONSTANT:
                return mKeyFrames.back().position; // Use last keyframe value
            default:
                Logger::log(1, "AssimpAnimChannel: postState %u not implemented for position\n", mPostState);
                return mKeyFrames.back().position;
        }
    }
    
    // Normal interpolation between keyframes
    int index = findKeyFrameIndex(animationTime);
    if (index >= 0 && index < static_cast<int>(mKeyFrames.size()) - 1) {
        float factor = getOptimizedScaleFactor(index, animationTime);
        return glm::mix(mKeyFrames[index].position, mKeyFrames[index + 1].position, factor);
    }
    
    return mKeyFrames[0].position; // Fallback
}

glm::quat AssimpAnimChannel::getInterpolatedRotation(float animationTime) {
    if (mKeyFrames.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion (no rotation)
    }
    
    if (mKeyFrames.size() == 1) {
        return mKeyFrames[0].rotation;
    }
    
    // Handle time before first keyframe (pre-animation behavior)
    if (animationTime < static_cast<float>(mKeyFrames[0].timeStamp)) {
        switch (static_cast<AnimBehavior>(mPreState)) {
            case AnimBehavior::DEFAULT:
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion (no rotation)
            case AnimBehavior::CONSTANT:
                return mKeyFrames[0].rotation; // Use first keyframe value
            default:
                Logger::log(1, "AssimpAnimChannel: preState %u not implemented for rotation\n", mPreState);
                return mKeyFrames[0].rotation;
        }
    }
    
    // Handle time after last keyframe (post-animation behavior)
    if (animationTime >= static_cast<float>(mKeyFrames.back().timeStamp)) {
        switch (static_cast<AnimBehavior>(mPostState)) {
            case AnimBehavior::DEFAULT:
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // Identity quaternion (no rotation)
            case AnimBehavior::CONSTANT:
                return mKeyFrames.back().rotation; // Use last keyframe value
            default:
                Logger::log(1, "AssimpAnimChannel: postState %u not implemented for rotation\n", mPostState);
                return mKeyFrames.back().rotation;
        }
    }
    
    // Normal interpolation between keyframes
    int index = findKeyFrameIndex(animationTime);
    if (index >= 0 && index < static_cast<int>(mKeyFrames.size()) - 1) {
        float factor = getOptimizedScaleFactor(index, animationTime);
        return glm::slerp(mKeyFrames[index].rotation, mKeyFrames[index + 1].rotation, factor);
    }
    
    return mKeyFrames[0].rotation; // Fallback
}

glm::vec3 AssimpAnimChannel::getInterpolatedScale(float animationTime) {
    if (mKeyFrames.empty()) {
        return glm::vec3(1.0f); // Identity scale (no scaling)
    }
    
    if (mKeyFrames.size() == 1) {
        return mKeyFrames[0].scale;
    }
    
    // Handle time before first keyframe (pre-animation behavior)
    if (animationTime < static_cast<float>(mKeyFrames[0].timeStamp)) {
        switch (static_cast<AnimBehavior>(mPreState)) {
            case AnimBehavior::DEFAULT:
                return glm::vec3(1.0f); // Identity scale (no scaling)
            case AnimBehavior::CONSTANT:
                return mKeyFrames[0].scale; // Use first keyframe value
            default:
                Logger::log(1, "AssimpAnimChannel: preState %u not implemented for scale\n", mPreState);
                return mKeyFrames[0].scale;
        }
    }
    
    // Handle time after last keyframe (post-animation behavior)
    if (animationTime >= static_cast<float>(mKeyFrames.back().timeStamp)) {
        switch (static_cast<AnimBehavior>(mPostState)) {
            case AnimBehavior::DEFAULT:
                return glm::vec3(1.0f); // Identity scale (no scaling)
            case AnimBehavior::CONSTANT:
                return mKeyFrames.back().scale; // Use last keyframe value
            default:
                Logger::log(1, "AssimpAnimChannel: postState %u not implemented for scale\n", mPostState);
                return mKeyFrames.back().scale;
        }
    }
    
    // Normal interpolation between keyframes
    int index = findKeyFrameIndex(animationTime);
    if (index >= 0 && index < static_cast<int>(mKeyFrames.size()) - 1) {
        float factor = getOptimizedScaleFactor(index, animationTime);
        return glm::mix(mKeyFrames[index].scale, mKeyFrames[index + 1].scale, factor);
    }
    
    return mKeyFrames[0].scale; // Fallback
}

float AssimpAnimChannel::getScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime) {
    float midWayLength = animationTime - lastTimeStamp;
    float framesDiff = nextTimeStamp - lastTimeStamp;
    return midWayLength / framesDiff;
}

float AssimpAnimChannel::getOptimizedScaleFactor(int keyFrameIndex, float animationTime) {
    // Use precalculated inverse time difference to avoid division (like reference implementation)
    if (keyFrameIndex >= 0 && keyFrameIndex < static_cast<int>(mInverseTimeDiffs.size())) {
        return (animationTime - static_cast<float>(mKeyFrames[keyFrameIndex].timeStamp)) * mInverseTimeDiffs[keyFrameIndex];
    }
    // Fallback to regular calculation if out of bounds
    return getScaleFactor(static_cast<float>(mKeyFrames[keyFrameIndex].timeStamp), 
                         static_cast<float>(mKeyFrames[keyFrameIndex + 1].timeStamp), 
                         animationTime);
}

int AssimpAnimChannel::findKeyFrameIndex(float animationTime) {
    // Performance tracking for debugging
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Use binary search to find the keyframe pair (O(log n))
    auto compareTime = [](const AnimationKeyFrame& keyFrame, float time) {
        return keyFrame.timeStamp < time;
    };
    
    auto it = std::lower_bound(mKeyFrames.begin(), mKeyFrames.end(), animationTime, compareTime);
    
    // Calculate index and ensure it's valid
    int index = static_cast<int>(std::distance(mKeyFrames.begin(), it)) - 1;
    
    // Update performance metrics
    auto endTime = std::chrono::high_resolution_clock::now();
    float searchTime = std::chrono::duration<float, std::micro>(endTime - startTime).count();
    mInterpolationCallCount++;
    mTotalSearchTime += searchTime;
    
    return std::max(index, 0);
}

void AssimpAnimChannel::optimizeMemoryLayout() {
    // Shrink containers to fit exact size (reduce memory footprint)
    mKeyFrames.shrink_to_fit();
    mInverseTimeDiffs.shrink_to_fit();
    
    // Sort keyframes by timestamp to ensure optimal cache locality
    // (This should already be sorted from Assimp, but ensure it)
    if (!mKeyFrames.empty()) {
        std::sort(mKeyFrames.begin(), mKeyFrames.end(), 
                 [](const AnimationKeyFrame& a, const AnimationKeyFrame& b) {
                     return a.timeStamp < b.timeStamp;
                 });
    }
    
    Logger::log(2, "AssimpAnimChannel: Memory optimized for '%s' - %zu keyframes, %zu time diffs\n",
               mTargetNodeName.c_str(), mKeyFrames.size(), mInverseTimeDiffs.size());
}

void AssimpAnimChannel::resetPerformanceMetrics() {
    mInterpolationCallCount = 0;
    mTotalSearchTime = 0.0f;
}

void AssimpAnimChannel::getPerformanceMetrics(size_t& interpolationCalls, float& averageSearchTime) const {
    interpolationCalls = mInterpolationCallCount;
    averageSearchTime = (mInterpolationCallCount > 0) ? (mTotalSearchTime / mInterpolationCallCount) : 0.0f;
}

} // namespace aveng 