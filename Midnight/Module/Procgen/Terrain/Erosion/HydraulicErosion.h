#pragma once
#include <cstdint>
#include <vector>
#include <future>

namespace aveng {
    struct AllPoints;
    struct Triangulation;
    struct HydraulicErosionParams;

    class ITaskSystem;
    class SpatialGrid;
}

namespace procgen {

	struct ErosionWorkingSet;

    // Submit hydraulic erosion batch work to the task system.
    // Returns one future per batch; each future yields a full-size local delta array.
    // Caller is responsible for polling readiness and calling ReduceHydraulicResults.
    std::vector<std::shared_future<std::vector<float>>> SubmitHydraulicBatches(
        ErosionWorkingSet& ws,
        const aveng::AllPoints& pts,
        const aveng::Triangulation& tri,
        const aveng::SpatialGrid& sg,
        const aveng::HydraulicErosionParams& cfg,
        uint64_t hydroSeed,
        aveng::ITaskSystem& tasks
    );

    // Accumulate completed batch deltas into ws.delta.
    // Precondition: every future in `batchFutures` is ready.
    void ReduceHydraulicResults(
        ErosionWorkingSet& ws,
        std::vector<std::shared_future<std::vector<float>>>& batchFutures
    );

}
