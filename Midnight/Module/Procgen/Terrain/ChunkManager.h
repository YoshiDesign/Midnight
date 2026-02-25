#pragma once
#include <array>
#include <stdint.h>
#include "Runtime/Threading/ITaskSystem.h"
#include "Module/Procgen/Types.h"
#include "Runtime/Threading/Types.h"
#include "Module/Procgen/Noise/Config.h"

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

    // Utility: chunk seed
    inline uint64_t chunkSeed(uint64_t worldSeed, ChunkCoord c) {
        // simple 64-bit mix; replace with your preferred hash
        uint64_t x = (uint32_t)c.x;
        uint64_t z = (uint32_t)c.z;
        uint64_t h = worldSeed ^ (x * 0x9E3779B185EBCA87ULL) ^ (z * 0xC2B2AE3D27D4EB4FULL);
        h ^= (h >> 30); h *= 0xBF58476D1CE4E5B9ULL;
        h ^= (h >> 27); h *= 0x94D049BB133111EBULL;
        h ^= (h >> 31);
        return h;
    }

	struct RecordPin; // forward declaration for pinning helper

    static constexpr size_t STRIPES = 64; // Up to 64 buckets for striped mutex maps

    class ChunkManager {
    public:

        explicit ChunkManager(ITaskSystem& tasks)
            : tasks_(tasks) {
            cfg_ = defaultTerrainConfig(); // Global Config
        }

        noise::NoiseParams defaultNoiseParams() {
            return {
                7,      // octaves
                0.01f,  // frequency
                5.0f,   // amplitude
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
				defaultNoiseParams(),
                true,
                true,
                true,
                true
            };
        }

        void setErosionManager(procgen::ErosionManager* er);

        // Very dangerous Public API (extend as needed, but work in tandem with pin/unpin)
        // Lifetime safety is paramount.
        std::shared_future<Points const*>           requestPoints(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<AllPoints const*>        requestAllPoints(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<HeightField const*>      requestHeights(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<Triangulation const*>    requestTriangulation(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<SpatialGrid const*>      requestSpatialGrid(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<ErosionField const*>     requestErosion(ChunkCoord c, uint64_t frameIndex);
        std::shared_future<FinalMeshCPU const*>     requestMesh(ChunkCoord c, uint64_t frameIndex);

        // Streaming helpers
        ChunkRecord* pin(ChunkCoord c, uint64_t frameIndex); // 
        void pin(ChunkRecord* rec, uint64_t frameIndex); // ptr pin
        void pin(ChunkRecord* rec); // touch
        void unpin(ChunkRecord* rec);
        void evictUnpinnedOlderThan(uint64_t frameIndex, uint64_t ageFrames);

        size_t stripeIndexFor(ChunkCoord c) const {
            return ChunkCoordHash{}(c) & (STRIPES - 1); // STRIPES must be power-of-two
                                                        // or: % STRIPES if not power-of-two
        }

    private:
        ChunkRecord* getOrCreateRecord(ChunkCoord c);

        procgen::ErosionManager* erosionMgr_ = nullptr;

        // Builders (run in worker threads)
        Points const*       buildPoints(ChunkRecord& r);        // alloc in final arena
        AllPoints const*    buildAllPoints(ChunkRecord& r);     // alloc in scratch
        HeightField const*  buildHeights(ChunkRecord& r);       // alloc in scratch
        Triangulation const* buildTriangulation(ChunkRecord& r); // alloc in scratch
		SpatialGrid const*  buildSpatialGrid(ChunkRecord& r);   // value owned by record
        ErosionField const* buildErosion(ChunkRecord& r, const ErosionSettings& settings);       // alloc in scratch
        FinalMeshCPU const* buildMesh(ChunkRecord& r);          // alloc in final

        ITaskSystem& tasks_;
        TerrainConfig cfg_; // Note that this differs from our prototype, where each Chunk owned its own config

		static constexpr size_t STRIPES = 64; // This should be a power of two for the bitwise bucket calculation to work correctly
        std::array<StripeBucket, STRIPES> records_;
    };

}