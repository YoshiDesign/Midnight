#pragma once
#include <memory_resource>
#include <vector>

namespace procgen {

	// Scratch resources for erosion passes. These are allocated in the chunk's scratch arena and reused across passes.
    struct ErosionWorkingSet {
        std::pmr::vector<float> workHeights; // mutable between passes
        std::pmr::vector<float> delta;       // reused per pass (Pattern A)
        std::pmr::vector<float> hardness;    // computed once, reused, then discarded
        std::pmr::vector<float> ping;        // Pattern B target buffer when needed

        explicit ErosionWorkingSet(std::pmr::memory_resource* scratchMr)
            : workHeights(scratchMr), delta(scratchMr), hardness(scratchMr), ping(scratchMr) {
        }
    };

}