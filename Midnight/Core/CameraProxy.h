#pragma once
#include "Utils/glm_includes.h"

/*
 * This header implements the idea that we can have a camera object
 * which updates its version anytime any of its properties are updated.
 * 
 * That way we could skip uploading UBO data when its unchanged.
 */

namespace aveng {
    struct CameraState {
        glm::vec3 position{ 0.f };
        glm::vec3 eulerYXZ{ 0.f };
        float fovRadians = glm::radians(50.f);
        float aspect = 16.f / 9.f, zNear = 0.1f, zFar = 1000.f;
    };

    class CameraProxy {
    public:
        void setPosition(const glm::vec3& p) { state_.position = p; ++version_; }
        void setEulerYXZ(const glm::vec3& r) { state_.eulerYXZ = r; ++version_; }
        void setLens(float fov, float a, float n, float f)
        {
            state_.fovRadians = fov; state_.aspect = a; state_.zNear = n; state_.zFar = f; ++version_;
        }

        CameraState snapshot() const { return state_; }   // cheap copy
        uint64_t version() const { return version_; } // plain integer

    private:
        CameraState state_{};
        uint64_t    version_ = 0;    // no atomics needed - but not thread-ready
    };
}