#pragma once
#include <array>
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
        std::shared_future<Points const*>           requestPoints(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<AllPoints const*>        requestAllPoints(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<HeightField const*>      requestHeights(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<Triangulation const*>    requestTriangulation(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<SpatialGrid const*>      requestSpatialGrid(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<ErosionField const*>     requestErosion(ChunkCoord c, uint64_t frameIndex);
        std::shared_future</*FinalMeshCPU const**/bool>     requestMesh(ChunkCoord c, uint64_t frameIndex);

        // Streaming helpers
        ChunkRecord* pin(ChunkCoord c, uint64_t frameIndex); // 
        void pin(ChunkRecord* rec, uint64_t frameIndex); // ptr pin
        void pin(ChunkRecord* rec); // touch
        void unpin(ChunkRecord* rec);
        void evictUnpinnedOlderThan(uint64_t frameIndex, uint64_t ageFrames);
        bool evictRecord(ChunkCoord coord);

        /* Managers */
        void initManagers(procgen::ErosionManager* er);

        /* Render Target & Completion Queue Drain */
        uint64_t requestRenderableAsync(ChunkCoord center, uint64_t frameIndex);
        bool tryTakeRenderable(ChunkCoord center, uint64_t requestId,
            std::unique_ptr<procgen::TerrainRenderable>& out);

        // Used after generation to acquire completed renderable 3x3 regions
        template <typename Fn>
        void drainCompletedRenderables(Fn&& fn)
        {
            completedRenderables_.drain(std::forward<Fn>(fn));
        }

        bool isAllPointsReady(const ChunkCoord coord) const;
        bool isRegionAllPointsReady(ChunkCoord center) const;
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
        void markAllPointsReady(ChunkCoord coord);
        void clearAllPointsReady(ChunkCoord coord);
        void markAllStagesComplete(ChunkCoord coord);
        void clearAllStagesComplete(ChunkCoord coord);

        // Sync Build Method
        std::unique_ptr<procgen::TerrainRenderable>
        generate(ChunkCoord center, uint64_t frameIndex, uint64_t requestId);

        /* Params, stage managers and Configs */
        void initManagerDefaults();

        mtools::ConcurrentQueue<procgen::RenderableCompletion> completedRenderables_;

        procgen::ErosionManager* erosionMgr_ = nullptr;

        // Non-blocking stage runners (check deps via isReady, re-enqueue if not ready)
        void runAllPointsStage(ChunkRecord& rec, uint64_t frameIndex);
        void runHeightsStage(ChunkRecord& rec, uint64_t frameIndex);
        void runTriangulationStage(ChunkRecord& rec, uint64_t frameIndex);
        void runSpatialGridStage(ChunkRecord& rec, uint64_t frameIndex);
        void runErosionStage(ChunkRecord& rec, const ErosionSettings& s, uint64_t frameIndex);
        void runMeshStage(ChunkRecord& rec, uint64_t frameIndex);
        void runGenerate(ChunkCoord center, uint64_t frameIndex, uint64_t requestId);

        // Builders (run in worker threads)
        Points const*       buildPoints(ChunkRecord& r);        // alloc in final arena
        AllPoints const*    buildAllPoints(ChunkRecord& r);     // alloc in scratch
        HeightField const*  buildHeights(ChunkRecord& r);       // alloc in scratch
        Triangulation const* buildTriangulation(ChunkRecord& r); // alloc in scratch
		SpatialGrid const*  buildSpatialGrid(ChunkRecord& r);   // value owned by record
        ErosionField const* buildErosion(ChunkRecord& r, const ErosionSettings& settings);       // alloc in scratch
        FinalMeshCPU const* buildMesh(ChunkRecord& r);          // alloc in final
        std::unique_ptr<procgen::TerrainRenderable> buildRenderablev2(ChunkCoord center, uint64_t frameIndex);
        std::unique_ptr<procgen::TerrainRenderable> buildRenderable(ChunkCoord center, uint64_t frameIndex);

        ThreadPoolTaskSystem& tasks_;
        TerrainConfig cfg_; // Note that this differs from our prototype, where each Chunk owned its own config
                            // Settings can be modulated during runtime to produce different results over time.
                            // There are per-stage settings too, naturally

		static constexpr size_t STRIPES = 64; // This should be a power of two for the bitwise bucket calculation to work correctly
        std::array<StripeBucket, STRIPES> records_;

        // Safe for parallel operations spanning overlapping regions
        // If we need to change which stage this implies that's super easy. For now its AllPoints generation.
        mutable std::mutex allPointsReadyMut_;
        std::unordered_set<ChunkCoord, ChunkCoordHash> allPointsReady_;

        mutable std::mutex allStagesCompleteMut_;
        std::unordered_set<ChunkCoord, ChunkCoordHash> allStagesComplete_;
    };

}