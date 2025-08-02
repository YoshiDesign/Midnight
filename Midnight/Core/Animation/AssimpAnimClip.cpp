#include "AssimpAnimClip.h"
#include "Logger.h"
#include "Tools.h"

#include <cmath>     // For fmod
#include <cstdlib>   // For std::abs

namespace aveng {

void AssimpAnimClip::addChannels(aiAnimation* animation, std::vector<std::shared_ptr<AssimpBone>> boneList) {
    mClipName = animation->mName.C_Str();
    mClipDuration = animation->mDuration;
    mClipTicksPerSecond = animation->mTicksPerSecond != 0 ? animation->mTicksPerSecond : 25.0f;
    
    Logger::log(1, "AssimpAnimClip: Loading animation '%s' with %d channels, duration: %.2f, ticks: %.2f\n", 
                mClipName.c_str(), animation->mNumChannels, mClipDuration, mClipTicksPerSecond);
    
    // Process each animation channel
    for (unsigned int i = 0; i < animation->mNumChannels; ++i) {
        aiNodeAnim* nodeAnim = animation->mChannels[i];
        std::string targetNodeName = nodeAnim->mNodeName.C_Str();
        
        auto channel = std::make_shared<AssimpAnimChannel>();
        channel->loadChannelData(nodeAnim);
        
        // Find corresponding bone ID
        for (size_t boneIndex = 0; boneIndex < boneList.size(); ++boneIndex) {
            if (boneList[boneIndex]->getBoneName() == targetNodeName) {
                channel->setBoneId(boneIndex);
                Logger::log(2, "AssimpAnimClip: Channel '%s' mapped to bone ID %d\n", 
                           targetNodeName.c_str(), static_cast<int>(boneIndex));
                break;
            }
        }
        
        mAnimChannels.push_back(channel);
    }
    
    Logger::log(1, "AssimpAnimClip: Successfully loaded %d animation channels\n", mAnimChannels.size());
}

const std::vector<std::shared_ptr<AssimpAnimChannel>>& AssimpAnimClip::getChannels() {
    return mAnimChannels;
}

std::string AssimpAnimClip::getClipName() {
    return mClipName;
}

float AssimpAnimClip::getClipDuration() {
    return mClipDuration;
}

float AssimpAnimClip::getClipTicksPerSecond() {
    return mClipTicksPerSecond;
}

void AssimpAnimClip::setClipName(std::string name) {
    mClipName = name;
}

} // namespace aveng 