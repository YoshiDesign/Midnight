#pragma once
#include "Core/Camera/aveng_camera.h"
#include "Game/Camera/ICameraDriver.h"
#include "Core/Input/InputState.h"
#include "avpch.h"

namespace aveng {
    using CameraId = uint32_t;

    struct CameraSlot {
        AvengCamera camera;
        CameraTransform   transform;
        std::unique_ptr<ICameraDriver> driver;
        std::string name;
        bool enabled = true;
    };

    struct CameraDebugInfo {
        std::string_view name;
        const CameraTransform& transform;
        bool active;
    };


    class CameraManager {
    public:

        CameraId createCamera(std::string name, std::unique_ptr<ICameraDriver> cameraDriver);
        void setActive(CameraId id) { 
            cameras_[active_].enabled = false;
            active_ = id; 
            cameras_[active_].enabled = true;
        };

        // const-correct override pair
        inline CameraSlot& active() { return cameras_[active_]; }
        inline const CameraSlot& active() const { return cameras_[active_]; } // when this* is const CameraManager - readonly
        inline const CameraId& activeId() const { return active_; }
        inline const int cameraCount() const { return cameras_.size(); }
  
        void update(float dt, const InputState& input) {
            auto& c = active();
            if (c.driver) // This check should be unnecessary given the invariance of this class
            {
                //std::cout << "Updating Driver!\t" << active_ << std::endl;
                c.driver->update(dt, input, c.transform);
            }

            // Interesting, that we apply calculations to the CameraSlot's transform member, 
            // and then apply it to the camera member's transform_. Yet another invariant /shrug
            c.camera.setTransform(c.transform);

        }

        // Access to all camera debug via iteration
        template<class Fn>
        void forEachCamera(Fn&& fn) const {
            for (const auto& c : cameras_) {
                fn(CameraDebugInfo{ c.name, c.transform, c.enabled });
            }
        }

    private:
        std::unordered_map<std::string, CameraTransform> transformsByName_; // Editor only, presumably
        std::vector<CameraSlot> cameras_;
        CameraId active_ = 0;
    };
}