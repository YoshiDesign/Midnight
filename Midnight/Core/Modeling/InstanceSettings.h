/* model specific settings */
#pragma once

#include <vector>
#include <memory>
#include "Utils/glm_includes.h"

namespace aveng {
    
    /* The instance's copy of its settings */
    struct InstanceTransform {
        glm::vec3 pos{ 0.f };
        glm::vec3 rotEuler{ 0.f };   // keep for now; later consider quat
        float     scale{ 1.f };
    };
    // write an overload to be able to compare two InstanceTransform structs
    inline bool operator==(const InstanceTransform& a, const InstanceTransform& b) {
        return a.pos == b.pos && a.rotEuler == b.rotEuler && a.scale == b.scale;
    }
    inline bool operator!=(const InstanceTransform& a, const InstanceTransform& b) noexcept {
        return !(a == b);
    }

    struct AnimSettings {
        size_t animClipSize{0};
        uint32_t clipNr{0};
        float    playTime{ 0.f }; // Somewhere between 0 and duration
        float    speed{ 1.f };
    };

    /* */
    struct TransformSettings {
        glm::vec3 worldPosition{ 0.f };
        glm::vec3 worldRotation{ 0.f };
        float     scale{ 1.f };
        bool      swapYZ{ false };
    };

    /* */
    struct AnimatedCreateSettings {
        TransformSettings transform;
        AnimSettings      anim;
    };

    struct MnMaterial {
        uint32_t baseTex{ 0 };
        uint32_t data_1{ 0 };
        uint32_t data_2{ 0 };
        //uint32_t normalTex;
        //uint32_t ormTex;
        //uint32_t emissiveTex;
        uint32_t ext_index{ 999 }; // Index into a larger struct of materials (InstanceMaterialB) if needed.
    };

    struct MnMaterialExt {
        uint32_t data_3{ 0 };
        uint32_t data_4{ 0 };
    };

}