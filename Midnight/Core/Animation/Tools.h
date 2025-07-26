#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <assimp/matrix4x4.h>
#include <assimp/vector3.h>
#include <assimp/quaternion.h>

namespace aveng {

/**
 * Utility functions for converting between Assimp and GLM types
 * Handles coordinate system conversion between Assimp (right-handed Y-up) 
 * and Midnight Engine (right-handed Z-forward, -Y-up)
 */
class Tools {
public:
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
     * Applies coordinate system conversion if needed
     */
    static glm::vec3 convertAiToGLM(const aiVector3D& aiVec, bool applyCoordConversion = true) {
        if (applyCoordConversion) {
            // Convert from Assimp (Y-up) to Midnight (Z-forward, -Y-up)
            return glm::vec3(aiVec.x, -aiVec.z, aiVec.y);
        }
        return glm::vec3(aiVec.x, aiVec.y, aiVec.z);
    }
    
    /**
     * Convert Assimp quaternion to GLM quaternion
     * Applies coordinate system conversion
     */
    static glm::quat convertAiToGLM(const aiQuaternion& aiQuat, bool applyCoordConversion = true) {
        if (applyCoordConversion) {
            // Convert from Assimp (Y-up) to Midnight (Z-forward, -Y-up)
            // Quaternion: (w, x, y, z) -> (w, x, -z, y)
            return glm::quat(aiQuat.w, aiQuat.x, -aiQuat.z, aiQuat.y);
        }
        return glm::quat(aiQuat.w, aiQuat.x, aiQuat.y, aiQuat.z);
    }
    
    /**
     * Create coordinate system conversion matrix
     * Converts from Assimp coordinate system to Midnight Engine coordinate system
     */
    static glm::mat4 getCoordinateConversionMatrix() {
        // Rotate -90 degrees around X to convert Y-up to Z-forward
        return glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    
    /**
     * Apply coordinate system conversion to a transformation matrix
     */
    static glm::mat4 applyCoordinateConversion(const glm::mat4& matrix) {
        glm::mat4 conversion = getCoordinateConversionMatrix();
        return conversion * matrix;
    }
};

} // namespace aveng 