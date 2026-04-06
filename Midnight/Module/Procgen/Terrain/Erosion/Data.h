#pragma once
#include <memory_resource>
#include <vector>
#include <chrono>
#include <future>
#include <cstdint>
#include "Module/Procgen/Types.h"
#include "Module/Procgen/Terrain/Control.h"

namespace procgen {

	template<typename T>
	static bool isReady(const std::shared_future<T>& fut) noexcept {
		if (!fut.valid()) return false;
		return fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
	}

	// Scratch resources for erosion passes. These are allocated in the chunk's scratch arena and reused across passes.
    struct ErosionWorkingSet {
        std::pmr::vector<float> workHeights; // mutable between passes
        std::pmr::vector<float> delta;       // reused per pass (Pattern A)
        std::pmr::vector<float> hardness;    // computed once, reused, then discarded
        std::pmr::vector<float> ping;        // Arbitrary "ping-pong" target buffer when needed

        explicit ErosionWorkingSet(std::pmr::memory_resource* scratchMr)
            : workHeights(scratchMr), delta(scratchMr), hardness(scratchMr), ping(scratchMr) {
        }
    };

    // -----------------------------------------------------------------------
    // Erosion build context -- persists across retry-enqueue cycles so that
    // runErosionStage can advance the erosion pipeline without blocking.
    // Stored on ChunkRecord; lifetime == one erosion build.
    // -----------------------------------------------------------------------
    struct ErosionBuildContext {

        enum class Phase : uint8_t {
            NotStarted,
            HydraulicSubmitted,
            ThermalSubmitted,
            RidgeSubmitted,
            Finalize,
            Done
        };

        Phase phase = Phase::NotStarted;
        uint32_t currentIteration = 0;

        // Batch futures for the active sub-stage.
        // Hydraulic + Thermal produce per-batch delta arrays; Ridge produces void.
        std::vector<std::shared_future<std::vector<float>>> valueFutures;
        std::vector<std::shared_future<void>>               voidFutures;

        // Ridge has 3 parallel sub-phases per iteration (ridgeness, copy, apply)
        uint8_t ridgeSubPhase = 0;

        // Working set persists across retries (allocated from rec.scratch)
        std::unique_ptr<ErosionWorkingSet> ws;

        // Settings snapshot (captured once in requestErosion)
        aveng::ErosionSettings settings{};

        // Per-stage deterministic seeds
        uint64_t hydroSeed    = 0;
        uint64_t thermalSeed  = 0;
        uint64_t ridgeSeed    = 0;

        // Thermal iteration state: pre-computed scheduling constants
        uint32_t thermalN       = 0;
        uint32_t thermalWorkers = 0;
        uint32_t thermalBatchSz = 0;

        // Ridge pre-computed constants
        uint32_t ridgeMaxWorkers = 0;
        float    ridgeMinH       = 0.f;
        float    ridgeMaxH       = 0.f;

        bool allValueFuturesReady() const {
            for (auto& f : valueFutures)
                if (!isReady(f)) return false;
            return true;
        }

        bool allVoidFuturesReady() const {
            for (auto& f : voidFutures)
                if (!isReady(f)) return false;
            return true;
        }

        void clearFutures() {
            valueFutures.clear();
            voidFutures.clear();
        }
    };

}