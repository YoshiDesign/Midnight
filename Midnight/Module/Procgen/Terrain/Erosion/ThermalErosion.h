#pragma once
#include <cstdint>
#include <vector>
#include <future>

namespace aveng {
    struct AllPoints;
    struct Triangulation;
    struct ThermalErosionParams;

    class ITaskSystem;
    class SpatialGrid;
}

namespace procgen {

	struct ErosionWorkingSet;

    // Prepare the working set for iterative thermal erosion.
    // Call once before the first iteration. Initializes ws.ping and returns
    // the computed worker count and batch size for subsequent submit calls.
    void InitThermalErosion(
        ErosionWorkingSet& ws,
        const aveng::ThermalErosionParams& cfg,
        uint32_t& outWorkers,
        uint32_t& outBatchSize
    );

    // Submit one iteration's batch work.
    // Reads from ws.ping (the iterative height state), returns per-batch local deltas.
    std::vector<std::shared_future<std::vector<float>>> SubmitThermalIterationBatches(
        ErosionWorkingSet& ws,
        const aveng::AllPoints& pts,
        const aveng::Triangulation& tri,
        const aveng::ThermalErosionParams& cfg,
        uint32_t workers,
        uint32_t batchSize,
        aveng::ITaskSystem& tasks
    );

    // Reduce completed batch futures for one iteration into ws.delta, then apply to ws.ping.
    // Precondition: every future in `batchFutures` is ready.
    void ReduceThermalIteration(
        ErosionWorkingSet& ws,
        std::vector<std::shared_future<std::vector<float>>>& batchFutures
    );

    // After all iterations: compute ws.delta = ws.ping - ws.workHeights
    // so the caller can ApplyDelta once.
    void FinalizeThermalDelta(ErosionWorkingSet& ws);

}
