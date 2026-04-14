#pragma once
#include <array>
#include <span>
#include <atomic>
#include <cstdint>
#include <unordered_set>
#include "Runtime/Threading/ITaskSystem.h"
#include "Module/Procgen/Noise/Config.h"
#include "Runtime/Threading/Types.h"
#include "Module/Procgen/Types2.h"
#include "Module/Procgen/Rendering/BasicTerrainAsset.h"
#include "Module/Procgen/Terrain/GpuResources.h"
#include "Module/Procgen/Terrain/Control.h"

namespace procgen {

    struct VkRenderData;
    struct Points;
    struct AllPoints;
    struct HeightField;
    struct Triangulation;
    struct ErosionField;
    struct FinalMeshCPU;
    struct ChunkRecord2;
    struct RecordPin;
    class  SpatialGrid;
    class  ThreadPoolTaskSystem;

	class ChunkManager2 {

    public:
        explicit ChunkManager2(
            ThreadPoolTaskSystem& tasks
#ifdef M_DEBUG
            , VkRenderData& renderData
#endif
        );

        // Relatively dangerous Public API (works in tandem with pin/unpin)
        void requestPoints(ChunkCoord c, uint64_t frameIndex);
        void requestAllPoints(ChunkCoord c, uint64_t frameIndex);
        void requestHeights(ChunkCoord c, uint64_t frameIndex);
        void requestTriangulation(ChunkCoord c, uint64_t frameIndex);
        void requestSpatialGrid(ChunkCoord c, uint64_t frameIndex);
        void requestErosion(ChunkCoord c, uint64_t frameIndex);
        void requestMesh(ChunkCoord c, uint64_t frameIndex);

        ChunkRecord2* getOrCreateRecord(ChunkCoord coord);

        /* Lifetime safety */
        ChunkRecord2* pin(ChunkCoord c, uint64_t frameIndex);
        void pin(ChunkRecord2* rec, uint64_t frameIndex);
        void pin(ChunkRecord2* rec);
        void unpin(ChunkRecord2* rec);

        TerrainPool terrain_pool;

        // Identity by coordinate
        std::unordered_map<ChunkCoord, ChunkHandle, ChunkCoordHash> coord_to_handle;

    };

}