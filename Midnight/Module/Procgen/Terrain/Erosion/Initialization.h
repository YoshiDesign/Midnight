#pragma once
#include <span>
#include <cstdint>
#include "Module/Procgen/Types.h"

namespace procgen {

	void ComputeHardnessMap(
        std::span<float> outHardness,
        std::span<const aveng::Vec2> positions,
        std::span<const float> heights,
        const aveng::HardnessParams& cfg,
        uint64_t seed);

}