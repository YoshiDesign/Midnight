#pragma once

#include <string>
#include <vector>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <assimp/anim.h>
#include "Logger.h"

namespace aveng {

// Animation behavior for times before first keyframe and after last keyframe
enum class AnimBehavior : unsigned int {
    DEFAULT = 0,    // Return zero/identity (don't change vertex position)
    CONSTANT = 1,   // Use value at first/last keyframe  
    LINEAR = 2,     // Linear extrapolation (not commonly used)
    REPEAT = 3      // Loop animation (not commonly used)
};

struct AnimationKeyFrame {
    double timeStamp;
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
};

class AssimpAnimChannel {
public:
    void loadChannelData(aiNodeAnim* nodeAnim);
    std::string getTargetNodeName();
    int getBoneId();
    void setBoneId(unsigned int id);
    
    // Get interpolated transformation at specific time
    glm::mat4 getTransformationMatrix(float animationTime);
    
    // Get individual components at specific time  
    glm::vec3 getInterpolatedPosition(float animationTime);
    glm::quat getInterpolatedRotation(float animationTime);
    glm::vec3 getInterpolatedScale(float animationTime);
    
    // Performance metrics and debugging
    void resetPerformanceMetrics();
    void getPerformanceMetrics(size_t& interpolationCalls, float& averageSearchTime) const;

private:
    std::string mTargetNodeName;
    int mBoneId = -1;
    
    std::vector<AnimationKeyFrame> mKeyFrames;
    std::vector<float> mInverseTimeDiffs;  // Precalculated 1.0f / (next - current) for performance
    
    // Edge case handling (like reference implementation)
    unsigned int mPreState = 1;   // aiAnimBehaviour_CONSTANT (default to first keyframe)
    unsigned int mPostState = 1;  // aiAnimBehaviour_CONSTANT (default to last keyframe)
    
    // Performance metrics for debugging
    mutable size_t mInterpolationCallCount = 0;
    mutable float mTotalSearchTime = 0.0f;
    
    // Helper functions for interpolation
    int findKeyFrameIndex(float animationTime);  // Binary search helper
    int findPositionKeyFrameIndex(float animationTime);
    int findRotationKeyFrameIndex(float animationTime);  
    int findScaleKeyFrameIndex(float animationTime);
    
    float getScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime);
    float getOptimizedScaleFactor(int keyFrameIndex, float animationTime);  // Uses cached inverse
    
    // Memory optimization methods
    void optimizeMemoryLayout();  // Reorganize data for better cache performance
};

} // namespace aveng