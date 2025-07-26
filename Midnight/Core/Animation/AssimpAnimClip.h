#pragma once

#include <string>
#include <vector>
#include <memory>

#include <assimp/anim.h>

#include "AssimpAnimChannel.h"
#include "AssimpBone.h"

namespace aveng {

class AssimpAnimClip {
public:
    void addChannels(aiAnimation* animation, std::vector<std::shared_ptr<AssimpBone>> boneList);
    const std::vector<std::shared_ptr<AssimpAnimChannel>>& getChannels();

    std::string getClipName();
    float getClipDuration();
    float getClipTicksPerSecond();

    void setClipName(std::string name);
    
    // Calculate local pose matrix for a specific bone at given time
    glm::mat4 getBoneTransformation(const std::string& boneName, float animationTime);
    
    // Get all bone transformations for current animation time
    void getBoneTransformations(float animationTime, std::vector<glm::mat4>& transformations, 
                               const std::vector<std::shared_ptr<AssimpBone>>& boneList);

private:
    std::string mClipName;
    float mClipDuration = 0.0f;
    float mClipTicksPerSecond = 0.0f;

    std::vector<std::shared_ptr<AssimpAnimChannel>> mAnimChannels{};
    
    // Helper to find channel by target node name
    std::shared_ptr<AssimpAnimChannel> findChannel(const std::string& nodeName);
};

} // namespace aveng
