#pragma once
#include "Game/Camera/ICameraDriver.h"
#include "Core/Camera/aveng_camera.h"
#include "Core/Input/InputState.h"

namespace aveng {
    class EditorCamera : public ICameraDriver {
    public:
        int id;
        void update(float dt, const InputState& input, CameraTransform& t) override;

    };
}