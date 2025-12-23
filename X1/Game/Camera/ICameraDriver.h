#pragma once
#include "Core/Camera/aveng_camera.h"
#include "Core/Input/InputState.h"
namespace aveng {

    struct ICameraDriver {
        virtual ~ICameraDriver() = default;
        virtual void update(float dt, const InputState& input, CameraTransform& inOutCameraTransform) = 0;
    };


    class OrbitCamera : public ICameraDriver {
    public:
        int targetId{};
        float distance = 5.f;
        glm::vec2 yawPitch{ 0.f };

        void update(float dt, const InputState& input, CameraTransform& t) override {
            // update yawPitch from input, compute position around target, set t.translation/t.rotation
        }
    };

    class PlayerCamera : public ICameraDriver {
    public:
        int id;
        void update(float dt, const InputState& input, CameraTransform& t) override;

    };



}