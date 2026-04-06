#pragma once
#include <array>
#include <span>
#include <stdint.h>
#include <unordered_set>
#include "Runtime/Threading/ITaskSystem.h"
#include "Module/Procgen/Types.h"
#include "Runtime/Threading/Types.h"
#include "Runtime/Threading/ConcurrentQueue.h"
#include "Module/Procgen/Noise/Config.h"
#include "Module/Procgen/Rendering/BasicTerrainAsset.h"
#include "Module/Procgen/Terrain/GpuResources.h"
#include "Module/Procgen/Terrain/Control.h"
#include "Core/Math/wyhash.h"

/*
 * TODO: Review scratch vs. final
 *
 * Notes - Organize these later.
 * Points location: rec.final (persistent, needed by neighbors later)
 * AllPoints location: rec.scratch (intermediate, only needed until triangulation)
 *     Why rec.scratch not tlsScratch?
 *      Heights stage needs to read rec.allPoints->pts
 *      Triangulation stage needs to read rec.allPoints->pts
 *      Can't use tlsScratch (resets after this job)
 *      Can't use rec.final (not a published artifact, discarded after mesh)
 * 
 * [IMPORTANT] DO NOT SET SETTINGS ON OTHER MANAGERS FROM THE CHUNKMANAGER.
 * TODO: Create a thinner interface rather than passing manager ref's into ChunkManager.
 */


// I still suck at organizing namespaces. This project is very long-lived, we'll get there.
namespace procgen {

    struct ErosionManager;

}

namespace aveng {

    struct Points;
    struct AllPoints;
    struct HeightField;
    struct Triangulation;
    struct ErosionField;
    struct FinalMeshCPU;
	struct ChunkRecord; // forward declaration for chunk record
	class SpatialGrid; // forward declaration for spatial grid product

	struct RecordPin; // forward declaration for pinning helper

    static constexpr size_t STRIPES = 64; // Up to 64 buckets for striped mutex maps

    class ChunkManager {
    public:

        explicit ChunkManager(ThreadPoolTaskSystem& tasks);

        /*
        * Note: Configs & Params are bound to balloon into a sub-system
        * 
        * - Noise Params live inside of TerrainConfig
        * - 
        */
        noise::NoiseParams defaultNoiseParams() {
            return {
                7,      // octaves
                0.005f,  // frequency
                32.0f,   // amplitude
                0.5f,   // persistence
                2.0f    // lacunarity
            };
        }

        TerrainConfig defaultTerrainConfig() {
            return {
                42,     // worldSeed
                256.f,  // chunkSize
                8.f,    // minPointDist
                32.f,   // halo
                tasks_.nThreads(),
				defaultNoiseParams()
            };
        }

#ifdef MIDNIGHT_WYHASH
        size_t stripeIndexwh(aveng::ChunkCoord c) {
            constexpr uint64_t Salt = 0xD1B54A32D192ED03ULL; // any constant or randomized at startup
            uint64_t h = aveng::wyhash64(Salt, packChunkCoord(c));
            return size_t(h) & (STRIPES - 1); // STRIPES must be a power of 2
        }
#endif

        // Very dangerous Public API (extend as needed, but work in tandem with pin/unpin)
        // Lifetime safety is paramount.
        void requestPoints(ChunkCoord c, uint64_t frameIndex);
        void requestAllPoints(ChunkCoord c, uint64_t frameIndex);
        void requestHeights(ChunkCoord c, uint64_t frameIndex);
        void requestTriangulation(ChunkCoord c, uint64_t frameIndex);
        void requestSpatialGrid(ChunkCoord c, uint64_t frameIndex);
        void requestErosion(ChunkCoord c, uint64_t frameIndex);
        void requestMesh(ChunkCoord c, uint64_t frameIndex);

        // Streaming helpers
        ChunkRecord* pin(ChunkCoord c, uint64_t frameIndex); // 
        void pin(ChunkRecord* rec, uint64_t frameIndex); // ptr pin
        void pin(ChunkRecord* rec); // touch
        void unpin(ChunkRecord* rec);
        void evictUnpinnedOlderThan(uint64_t frameIndex, uint64_t ageFrames);
        bool evictRecord(ChunkCoord coord);

        /* Managers */
        void initManagers(procgen::ErosionManager* er);

        void setAdmissionController(procgen::TerrainAdmissionController* ac, int supportRadius) {
            admissionCtl_ = ac;
            admissionRadius_ = supportRadius;
        }

        /* Render Target & Completion Queue Drain */
        uint64_t requestRenderableAsync(ChunkCoord center, uint64_t frameIndex,
            procgen::TerrainRenderable* target, uint32_t slotIndex);

        template <typename Fn>
        void drainCompletedRenderables(Fn&& fn)
        {
            completedRenderables_.drain(std::forward<Fn>(fn));
        }

        bool isSpatialGridReady(const ChunkCoord coord) const;
        bool isRegionSpatialGridReady(ChunkCoord center) const;
        bool isRegionAllStagesComplete(ChunkCoord center) const;

        // Used during generation to acquire the record we need
        ChunkRecord* getOrCreateRecord(ChunkCoord c);

        /* Config/Settings */
        void setGlobalConfig(TerrainConfig& tcfg) {
            cfg_.worldSeed = tcfg.worldSeed;
            cfg_.chunkSize = tcfg.chunkSize;
            cfg_.halo = tcfg.halo;
            cfg_.minPointDist = tcfg.minPointDist;
        }

        void setNoiseParams(noise::NoiseParams noise) {
            std::printf("Amplitude: %f\tFrequency: %f\n", noise.amplitude, noise.frequency);
            std::printf("Updated Noise\n");
            cfg_.noise = noise;
        }

        void setErosionParameters(ErosionSettings eroCfg);

        /* Getter */
        float chunkSize() {
            return cfg_.chunkSize;
        }

    private:
        void markSpatialGridReady(ChunkCoord coord);
        void clearSpatialGridReady(ChunkCoord coord);
        void markAllStagesComplete(ChunkCoord coord);
        void clearAllStagesComplete(ChunkCoord coord);

        // Sync Build Method (deprecated -- use runGenerate)
        void generate(ChunkCoord center, uint64_t frameIndex, uint64_t requestId);

        /* Params, stage managers and Configs */
        void initManagerDefaults();

        // The concurrent queue processes our completion signals.
        mtools::ConcurrentQueue<procgen::CompletionNotice> completedRenderables_;

        procgen::TerrainAdmissionController* admissionCtl_ = nullptr;
        int admissionRadius_ = 0;

        procgen::ErosionManager* erosionMgr_ = nullptr;

        // Non-blocking stage runners (check deps via isReady, re-enqueue if not ready)
        void runAllPointsStage(ChunkRecord& rec, uint64_t frameIndex);
        void runHeightsStage(ChunkRecord& rec, uint64_t frameIndex);
        void runTriangulationStage(ChunkRecord& rec, uint64_t frameIndex);
        void runSpatialGridStage(ChunkRecord& rec, uint64_t frameIndex);
        void runErosionStage(ChunkRecord& rec, uint64_t frameIndex);
        void runMeshStage(ChunkRecord& rec, uint64_t frameIndex);
        void runGenerate(ChunkCoord center, uint64_t frameIndex, uint64_t requestId);

        // Builders (run in worker threads)
        Points const*       buildPoints(ChunkRecord& r);        // alloc in final arena
        AllPoints const*    buildAllPoints(ChunkRecord& r);     // alloc in scratch
        HeightField const*  buildHeights(ChunkRecord& r);       // alloc in scratch
        Triangulation const* buildTriangulation(ChunkRecord& r); // alloc in scratch
		SpatialGrid const*  buildSpatialGrid(ChunkRecord& r);   // value owned by record
        bool advanceErosion(ChunkRecord& r);  // retry-driven state machine; returns true when complete
        // FinalMeshCPU const* buildMesh(ChunkRecord& r);          // alloc in final
        void buildRenderablev2(ChunkCoord center, uint64_t frameIndex,
            std::span<ChunkRecord*, 25> recs);
        // std::unique_ptr<procgen::TerrainRenderable> buildRenderable(ChunkCoord center, uint64_t frameIndex);

        ThreadPoolTaskSystem& tasks_;
        TerrainConfig cfg_; // Note that this differs from our prototype, where each Chunk owned its own config
                            // Settings can be modulated during runtime to produce different results over time.
                            // There are per-stage settings too, naturally

		static constexpr size_t STRIPES = 64; // This should be a power of two for the bitwise bucket calculation to work correctly
        std::array<StripeBucket, STRIPES> records_;

        // Safe for parallel operations spanning overlapping regions
        // If we need to change which stage this implies that's super easy. For now its AllPoints generation.
        mutable std::mutex allSpatialGridReadyMut_;
        std::unordered_set<ChunkCoord, ChunkCoordHash> allSpatialGridReady_;

        mutable std::mutex allStagesCompleteMut_;
        std::unordered_set<ChunkCoord, ChunkCoordHash> allStagesComplete_;
    };

}