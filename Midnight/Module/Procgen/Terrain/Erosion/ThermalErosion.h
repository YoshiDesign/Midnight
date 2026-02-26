#pragma once
#include <cstdint>

namespace aveng {
    struct AllPoints;
    struct Triangulation;
    struct ThermalErosionParams;

    class ITaskSystem;
    class SpatialGrid;
}

namespace procgen {

	struct ErosionWorkingSet;
    // ---- The actual pass ----
    void ComputeThermalErosion(
        ErosionWorkingSet& ws,
        const aveng::AllPoints& pts,
        const aveng::Triangulation& tri,
        const aveng::SpatialGrid& sg,
        const aveng::ThermalErosionParams& cfg,
        uint64_t thermalSeed,
        aveng::ITaskSystem& tasks
    );

}
