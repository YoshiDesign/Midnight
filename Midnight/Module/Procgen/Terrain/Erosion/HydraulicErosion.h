#pragma once
#include <cstdint>

namespace aveng {
    struct AllPoints;
    struct Triangulation;
    struct HydraulicErosionParams;

    class ITaskSystem;
    class SpatialGrid;
}

namespace procgen {

	struct ErosionWorkingSet;

    void ComputeHydraulicErosion(
        ErosionWorkingSet& ws,
        const aveng::AllPoints& pts,
        const aveng::Triangulation& tri,
        const aveng::SpatialGrid& sg,
        const aveng::HydraulicErosionParams& cfg,
        uint64_t hydroSeed,
        aveng::ITaskSystem& tasks
    );

}