#pragma once
#include <cstdint>
#include <vector>
#include <future>

namespace aveng {
    struct AllPoints;
    struct Triangulation;
    struct RidgeParams;

    class ITaskSystem;
    class SpatialGrid;
}

namespace procgen {
    struct ErosionWorkingSet;

    // Prepare working set for ridge enhancement iterations.
    // Computes min/max heights and returns the effective maxWorkers.
    void InitRidgeEnhancement(
        ErosionWorkingSet& ws,
        const aveng::RidgeParams& cfg,
        float& outMinH,
        float& outMaxH,
        uint32_t& outMaxWorkers
    );

    // Sub-phase 0: compute ridgeness values into ws.delta (parallel).
    std::vector<std::shared_future<void>> SubmitRidgenessCompute(
        ErosionWorkingSet& ws,
        const aveng::Triangulation& tri,
        uint32_t maxWorkers,
        aveng::ITaskSystem& tasks
    );

    // Sub-phase 1: copy ws.workHeights into ws.ping (parallel).
    std::vector<std::shared_future<void>> SubmitRidgeCopy(
        ErosionWorkingSet& ws,
        uint32_t maxWorkers,
        aveng::ITaskSystem& tasks
    );

    // Sub-phase 2: apply ridge boost + noise into ws.ping (parallel).
    std::vector<std::shared_future<void>> SubmitRidgeApply(
        ErosionWorkingSet& ws,
        const aveng::AllPoints& allPts,
        const aveng::RidgeParams& cfg,
        uint64_t ridgeSeed,
        float minH,
        float maxH,
        uint32_t maxWorkers,
        aveng::ITaskSystem& tasks
    );

    // Swap ping <-> workHeights after one iteration completes.
    void SwapRidgeIteration(ErosionWorkingSet& ws);

}
