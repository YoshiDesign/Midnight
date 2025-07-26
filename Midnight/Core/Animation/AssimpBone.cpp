#include "AssimpBone.h"

namespace aveng {

AssimpBone::AssimpBone(unsigned int id, std::string name, glm::mat4 matrix) 
    : mBoneId(id), mNodeName(name), mOffsetMatrix(matrix) {
}

unsigned int AssimpBone::getBoneId() {
    return mBoneId;
}

std::string AssimpBone::getBoneName() {
    return mNodeName;
}

glm::mat4 AssimpBone::getOffsetMatrix() {
    return mOffsetMatrix;
}

} // namespace aveng
