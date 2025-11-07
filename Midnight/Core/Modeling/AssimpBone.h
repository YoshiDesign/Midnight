#pragma once

#include <vector>
#include "Utils/glm_includes.h"
#include <assimp/anim.h>
namespace aveng {
    class AssimpBone {
    public:
        AssimpBone(unsigned int id, std::string name, glm::mat4 matrix);

        unsigned int getBoneId();
        std::string getBoneName();
        glm::mat4 getOffsetMatrix();

    private:
        unsigned int mBoneId = 0;
        std::string mNodeName;
        glm::mat4 mOffsetMatrix = glm::mat4(1.0f);
    };
}