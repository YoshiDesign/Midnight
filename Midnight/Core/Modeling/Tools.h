#pragma once
#include "Utils/glm_includes.h"
#include <assimp/matrix4x4.h>
#include <assimp/vector3.h>
#include <assimp/quaternion.h>
#include <cstring>
#include <string>

namespace aveng {

    /**
     * Utility functions for converting between Assimp and GLM types
     * Handles coordinate system conversion between Assimp (right-handed Y-up)
     * and Midnight Engine (right-handed Z-forward, -Y-up)
     */
    class Tools {
    public:

        static std::string getFilenameExt(std::string filename);
        static std::string loadFileToString(std::string fileName);
        /**
         * Convert Assimp 4x4 matrix to GLM matrix
         * Handles coordinate system conversion
         */
        static glm::mat4 convertAiToGLM(const aiMatrix4x4& aiMat) {
            glm::mat4 result(
                aiMat.a1, aiMat.b1, aiMat.c1, aiMat.d1,
                aiMat.a2, aiMat.b2, aiMat.c2, aiMat.d2,
                aiMat.a3, aiMat.b3, aiMat.c3, aiMat.d3,
                aiMat.a4, aiMat.b4, aiMat.c4, aiMat.d4
            );
            return result;
        }

        /**
         * Convert Assimp vector3 to GLM vec3
         * No coordinate conversion applied - use gltfToEngine matrix for that
         */
        static glm::vec3 convertAiToGLM(const aiVector3D& aiVec) {
            return glm::vec3(aiVec.x, aiVec.y, aiVec.z);
        }

        /**
         * Convert Assimp quaternion to GLM quaternion
         * No coordinate conversion applied - use gltfToEngine matrix sandwich for that
         */
        static glm::quat convertAiToGLM(const aiQuaternion& aiQuat) {
            return glm::quat(aiQuat.w, aiQuat.x, aiQuat.y, aiQuat.z);
        }

        /**
         * Midnight Engine coordinate system basis: -Y up, +Z forward, +X right
         * 
         * glTF/Assimp uses: +Y up, -Z forward, +X right
         * This matrix converts from glTF to engine coordinates by flipping Y and Z.
         * 
         * For transforming:
         *   - Points/vectors: B * v
         *   - Matrices (rotations, transforms): B * M * inverse(B)
         */
        inline static const glm::mat4 gltfToEngine = glm::mat4(
            1, 0, 0, 0,
            0, -1, 0, 0,
            0, 0, -1, 0,
            0, 0, 0, 1
        );

    };

} // namespace aveng 