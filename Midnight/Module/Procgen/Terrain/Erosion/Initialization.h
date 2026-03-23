#pragma once
#include <span>
#include <cstdint>
#include "Module/Procgen/Types.h"

/**
 * Not quite succinctly named, but this TU is meant to provide any up-front initialization
 * on behalf of any procedural generation stage that may benefit from some initialization step
 */

namespace procgen {

    /* Provide the erosion pass with a hardness weight-map */
	void ComputeHardnessMap(
        std::span<float> outHardness,
        std::span<const aveng::Vec2> positions,
        std::span<const float> heights,
        const aveng::HardnessParams& cfg,
        uint64_t seed);

}