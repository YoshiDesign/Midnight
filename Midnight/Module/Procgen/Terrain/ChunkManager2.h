#pragma once
#include <array>
#include <span>
#include <atomic>
#include <cstdint>
#include <unordered_set>
#include "Runtime/Threading/ITaskSystem.h"
#include "Module/Procgen/Terrain/Control.h"
#include "Module/Procgen/Noise/Config.h"
#include "Module/Procgen/Types2.h"
#include "Runtime/Memory/Arena.h"
#include "Runtime/Threading/Scratch.h"
#include "Runtime/Threading/Types.h"
// #include "Module/Procgen/Rendering/BasicTerrainAsset.h" // We might only need to reuse the draw/gpu structs from here
#include "Module/Procgen/Terrain/GpuResources.h"
#include "Module/Procgen/Noise/Config.h"

namespace aveng {
    struct VkRenderData;
}

namespace procgen {

    struct Points;
    struct AllPoints;
    struct HeightField;
    struct Triangulation;
    struct ErosionField;
    struct FinalMeshCPU;
    struct ChunkRecord2;
    struct RecordPin;
    class  SpatialGrid2;
    struct ErosionManager;

	class ChunkManager2 {

    public:
        explicit ChunkManager2(
            aveng::ThreadPoolTaskSystem& tasks
#ifdef M_DEBUG
            , aveng::VkRenderData& renderData
#endif
        );

        ~ChunkManager2();

        aveng::noise::NoiseParams defaultNoiseParams() {
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
       
        /* Config/Settings */
        void setGlobalConfig(TerrainConfig& tcfg) {
            cfg_.worldSeed = tcfg.worldSeed;
            cfg_.chunkSize = tcfg.chunkSize;
            cfg_.halo = tcfg.halo;
            cfg_.minPointDist = tcfg.minPointDist;
        }

        void setNoiseParams(aveng::noise::NoiseParams noise) {
            std::printf("Amplitude: %f\tFrequency: %f\n", noise.amplitude, noise.frequency);
            std::printf("Updated Noise\n");
            cfg_.noise = noise;
        }

        void setErosionParameters(ErosionSettings eroCfg);

        /* Getter */
        float chunkSize() {
            return cfg_.chunkSize;
        }

        void init(aveng::Arena* arena);

        /* Managers */
        void initManagers(ErosionManager* er);

        void initManagerDefaults();

        void setAdmissionController(TerrainAdmissionController* ac, int supportRadius) {
            admissionCtl_ = ac;
            admissionRadius_ = supportRadius;
        }

        void setTerrainPool(TerrainPool* pool) {
            terrain_pool = pool;
		}

        // Relatively dangerous Public API (works in tandem with pin/unpin)
        void requestPoints(ChunkCoord c, uint64_t frameIndex);
        void requestAllPoints(ChunkCoord c, uint64_t frameIndex);
        void requestHeights(ChunkCoord c, uint64_t frameIndex);
        void requestTriangulation(ChunkCoord c, uint64_t frameIndex);
        void requestSpatialGrid(ChunkCoord c, uint64_t frameIndex);
        void requestErosion(ChunkCoord c, uint64_t frameIndex);
        void requestMesh(ChunkCoord c, uint64_t frameIndex);

        ChunkRecord2* getOrCreateRecord(ChunkCoord coord, uint64_t frameIndex);

        uint64_t requestRenderableAsync(ChunkCoord center, uint64_t frameIndex, procgen::TerrainRenderable* target, uint32_t slotIndex);

        void runGenerate(ChunkCoord center, uint64_t frameIndex, uint64_t requestId);

        void runAllPointsStage(ChunkRecord2& rec, uint64_t frameIndex);

        Points const* buildPoints(ChunkRecord2& rec);

        AllPoints const* buildAllPoints(ChunkRecord2& rec);

        bool isSpatialGridReady(const ChunkCoord coord) const;

        bool isRegionSpatialGridReady(ChunkCoord center) const;

        bool isRegionAllStagesComplete(ChunkCoord center) const;

        /* Lifetime safety */
        ChunkRecord2* pin(ChunkCoord c, uint64_t frameIndex);
        void pin(ChunkRecord2* rec, uint64_t frameIndex);
        void pin(ChunkRecord2* rec);
        void unpin(ChunkRecord2* rec);

        // Identity by coordinate
        std::unordered_map<ChunkCoord, ChunkHandle, ChunkCoordHash> coord_to_handle;

        /* Memory Resource */
        aveng::Arena* terrain_arena{};
        TerrainPool* terrain_pool = nullptr;

		// I don't think I should ever use these pointers, they're just here... use slots to access each base directly
        std::byte* scratch_space;
        std::byte* final_space;

        aveng::ThreadPoolTaskSystem& tasks_;

        TerrainAdmissionController* admissionCtl_ = nullptr;
        int admissionRadius_ = 0;
        
        ErosionManager* erosionMgr_ = nullptr;

        TerrainConfig cfg_; // Note that this differs from our prototype, where each Chunk owned its own config
                            // Settings can be modulated during runtime to produce different results over time.
                            // There are per-stage settings too, naturally
#ifdef M_DEBUG
        aveng::VkRenderData& renderData_;
#endif
    };

}