#include "ChunkManager2.h"

#ifdef M_DEBUG
#include <filesystem>
#include <string>
#include <cstdint>
#include <format>
#include "Runtime/Debug.h"
#endif
#include "CoreVK/VkRenderData.h"
#include "Module/Procgen/Terrain/Erosion/Data.h"
#include "Module/Procgen/Terrain/Erosion/ErosionManager.h"
#include "Module/Procgen/Terrain/Erosion/HydraulicErosion.h"
#include "Module/Procgen/Terrain/Erosion/RidgeEnhancement.h"
#include "Module/Procgen/Terrain/Erosion/ThermalErosion.h"
#include "Module/Procgen/Terrain/TerrainPool.h"
#include "Module/Procgen/Terrain/ChunkRecord2.h"

namespace {

    // Get 3x3 neighborhood coordinates (including self at center)
    void get3x3Neighborhood(aveng::ChunkCoord center, aveng::ChunkCoord out[9]) noexcept {
        out[0] = { center.x - 1, center.z - 1 };
        out[1] = { center.x,     center.z - 1 };
        out[2] = { center.x + 1, center.z - 1 };
        out[3] = { center.x - 1, center.z };
        out[4] = { center.x,     center.z };
        out[5] = { center.x + 1, center.z };
        out[6] = { center.x - 1, center.z + 1 };
        out[7] = { center.x,     center.z + 1 };
        out[8] = { center.x + 1, center.z + 1 };
    }

    // Get 5x5 neighborhood: indices [0..8] are the inner 3x3, [9..24] are the outer ring.
    void get5x5Neighborhood(aveng::ChunkCoord center, aveng::ChunkCoord out[25]) noexcept {
        // Inner 3x3 first (same layout as get3x3Neighborhood)
        get3x3Neighborhood(center, out);
        // Outer ring (16 chunks)
        int idx = 9;
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                if (dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1) continue;
                out[idx++] = { center.x + dx, center.z + dz };
            }
        }
    }

}

namespace procgen {

    struct RecordPin {
        ChunkManager2* mgr{};
        ChunkRecord2* rec{};

        RecordPin() = default;

        RecordPin(ChunkManager2& m, ChunkRecord2* r, uint64_t frameIndex)
            : mgr(&m), rec(r)
        {
            // std::printf("Pinning chunk record for ChunkCoord(%d, %d)\n", r->coord.x, r->coord.z);
            mgr->pin(rec, frameIndex); // increments + touches
        }

        RecordPin(ChunkManager2& m, ChunkRecord2* r)
            : mgr(&m), rec(r)
        {
            mgr->pin(rec); // touches
        }

        ~RecordPin() {
            if (rec) mgr->unpin(rec);
        }

        RecordPin(RecordPin&& o) noexcept : mgr(o.mgr), rec(o.rec) {
            o.rec = nullptr;
        }

        RecordPin(const RecordPin&) = delete;
        RecordPin& operator=(const RecordPin&) = delete;
        RecordPin& operator=(RecordPin&& o) noexcept {
            if (this != &o) {
                if (rec) mgr->unpin(rec);
                mgr = o.mgr;
                rec = o.rec;
                o.rec = nullptr;
            }
            return *this;
        }
    };

    ChunkManager2::ChunkManager2(
        aveng::ThreadPoolTaskSystem& tasks
#ifdef M_DEBUG
        , aveng::VkRenderData& renderData
#endif

    )
        : tasks_(tasks)
#ifdef M_DEBUG
        , renderData_(renderData)
#endif
    {
        cfg_ = defaultTerrainConfig(); // Global Config
        cfg_.noise = defaultNoiseParams();
    }


    ChunkManager2::~ChunkManager2()
    {
        // Invalidate the pointer, Midnight will destroy the arena properly from its dtor.
        aveng::ArenaReset(terrain_arena);
        terrain_arena = nullptr;
    }

    void ChunkManager2::init(aveng::Arena* arena) {
        // Arena already allocated by Midnight::initialize
        terrain_arena = arena;
        aveng::ArenaReset(terrain_arena);
		ChunkRecord2* chunkRecords = (ChunkRecord2*)aveng::ArenaAlloc(terrain_arena, sizeof(ChunkRecord2) * terrain_pool->capacity);
    }

    /* Manager Setups */
    void ChunkManager2::initManagers(ErosionManager* er)
    {
        // So far we only have an ErosionManager
        erosionMgr_ = er;
        initManagerDefaults();
    }

    void ChunkManager2::initManagerDefaults()
    {
        // nThreads is required for init.
        if (!erosionMgr_->switchToDefaultSettings(cfg_.nThreads)) {
			throw std::runtime_error("failed to initialize default erosion settings");
        }
    }


    /* Lifetime saftey features & eviction policy (below) */

    ChunkRecord2* ChunkManager2::getOrCreateRecord(ChunkCoord coord)
    {

        // TODO : Use release / acquire atomic to create a record:
        // 1. Using the coord, check coord_to_handle
        // - If already exists:
        //      - return handle->slot_->record
        // - Else:
        //      - 
        // 1 look up coord in map
        // 2 if found, validate handle and return record
        // 3 otherwise allocate a slot, create record, insert into map, return handle

        return ;
    }

    // Pin based on chunk coord - this can end up creating a chunk record
    ChunkRecord2* ChunkManager2::pin(ChunkCoord c, uint64_t frameIndex) {
        ChunkRecord2* rec = getOrCreateRecord(c);
        //rec->pinCount++;
        //rec->lastTouchedFrame = frameIndex;
        return rec;
    }

    // Pin via pointer - for when we already have the record
    void ChunkManager2::pin(ChunkRecord2* rec, uint64_t frameIndex) {
        //rec->pinCount++;
        //rec->lastTouchedFrame = frameIndex;
    }

    // Pin without perturbing frame count (touch)
    void ChunkManager2::pin(ChunkRecord2* rec) {
       // rec->pinCount++;
    }

    // Unpin from pointer - Note: Don't ever do unpin(ChunkCoord) because passing a coord implies we'd call getOrCreateRecord (at the moment)
    void ChunkManager2::unpin(ChunkRecord2* rec) {
        // rec->pinCount++;
    }
}