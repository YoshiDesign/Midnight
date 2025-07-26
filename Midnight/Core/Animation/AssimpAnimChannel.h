#pragma once

#include <string>
#include <vector>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <assimp/anim.h>

namespace aveng {

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

private:
    std::string mTargetNodeName;
    int mBoneId = -1;
    
    std::vector<AnimationKeyFrame> mKeyFrames;
    
    // Helper functions for interpolation
    int findPositionKeyFrameIndex(float animationTime);
    int findRotationKeyFrameIndex(float animationTime);  
    int findScaleKeyFrameIndex(float animationTime);
    
    float getScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime);
};

} // namespace aveng