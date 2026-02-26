#pragma once
#include <cstdint>

namespace aveng {
    struct AllPoints;
    struct Triangulation;
    struct RidgeParams;

    class ITaskSystem;
    class SpatialGrid;
}

namespace procgen {
    struct ErosionWorkingSet;

    void ComputeRidgeEnhancement(
        ErosionWorkingSet& ws,
        const aveng::AllPoints& allPts,
        const aveng::Triangulation& tri,
        const aveng::SpatialGrid& /*sg*/,
        const aveng::RidgeParams& cfg,
        uint64_t ridgeSeed, // The thermal seed is unused bc there's no rng here. We could add some jitter/noise or other behaviors in the future.
        aveng::ITaskSystem& tasks
    );

}