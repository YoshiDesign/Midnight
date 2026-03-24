#include <cstring>
#include <mutex>
#include "ChunkManager.h"
#include "avpch.h"
#include "Core/Math/quantize.h"
#include "Runtime/Threading/Scratch.h"
#include "Module/Procgen/Noise/Bluenoise.h"
#include "Module/Procgen/Noise/Functions.h"
#include "Module/Procgen/Delaunay.h"
#include "Module/Procgen/Rendering/BasicTerrainAsset.h"
#include "Module/Procgen/Terrain/Erosion/Data.h"
#include "Module/Procgen/Terrain/Erosion/ErosionManager.h"
#include "Module/Procgen/Terrain/Erosion/HydraulicErosion.h"
#include "Module/Procgen/Terrain/Erosion/RidgeEnhancement.h"
#include "Module/Procgen/Terrain/Erosion/ThermalErosion.h"
#include "Module/Procgen/Terrain/Erosion/Initialization.h"

#ifdef M_DEBUG
#include <filesystem>
#include <string>
#include <cstdint>
#include <format>
#include "Runtime/Debug.h"
#endif

/*
* Policy
* - pointers returned by futures are only valid while the chunk is pinned.
* - Each stage function should only request the minimum prerequisite stage(s).
*   This is guaranteed by cascading through requested stages.
*
* Design Notes:
* - Anything you publish (return from a future) must be allocated in rec.final
* - Scratch is per-thread and reset every job without risking published pointers
* - Each ChunkRecord has its own scratch storage as well.
*
* Threads:
* Each OS thread gets its own independent instance of tlsScratch.
* - When worker threads run a task, each uses its own scratch arena.
* - No locking, no contention, no cross-thread memory reuse bugs.
* - If you ever run tasks on the main thread too (e.g. "helping wait" executes work inline),
*   the main thread will have its own tlsScratch instance as well.
*
* Safety:
*   - pin() is coupled to ChunkRecord generation, but not the other way around.
*     We're using a strict lifetime policy at the moment.
*
* We pin at each individual stage. Why, you ask?
* While stages can be requested independently, you want those stage tasks to
* be safe even when no higher-level "pipeline pin" exists.
*
* We only "pipeline pin" because we want everything to occur before eviction becomes safe.
*
* Future Consideration:
* - Generate halo points deterministically without reading neighbors:
*     Sometimes people avoid halo stitching by generating points from a world seed for the expanded region and then
*     selecting the core subset per chunk. That's elegant but changes the architecture: each chunk
*     would be responsible for generating points in its expanded bounds, which we explicitly avoid at the moment.
*/

/*
* Notes from Chat-Guy
*
* Returning ChunkRecord* is safe as long as eviction respects pins (lifetime safety!).
*
* If we want to squeeze a little more perf from this:
*   If creation is heavy (it isn't too heavy here, but it's not free), you could tighten
*   the critical section later (create outside the lock), but doing that safely requires a
*   more careful "placeholder/state" approach so other threads don't see a half-initialized record.
*   (Worth doing only if you measure it as a bottleneck.)
*   Also, we can keep the bucket lock short by inserting a "shell" record quickly, then doing heavier
*   initialization outside the lock
*/

namespace {

    struct TriRef
    {
        aveng::ChunkRecord* rec = nullptr;
        uint32_t localTriIndex = 0;
        uint8_t neighborIdx = 0;
    };

    // TODO - Idk where to put this yet. I'm sure we'll land on a convention
    // as procgen grows.
    void ApplyDelta(std::span<float> work, std::span<const float> delta) {
#ifdef M_DEBUG
        // assert(work.size() == delta.size() && "ApplyDelta: size mismatch");
#endif
        for (size_t i = 0; i < work.size(); ++i) {
            work[i] += delta[i];
        }

    }

    bool pointInRectHalfOpen(const aveng::Vec2 centroid, const aveng::Bounds2 bounds) {
        return (centroid.x >= bounds.minX && centroid.x < bounds.maxX) &&
               (centroid.y >= bounds.minZ && centroid.y < bounds.maxZ);
    }

    // For chunk->triangle ownership
    aveng::Vec2 triangleCentroid(
        const aveng::Vec2& a,
        const aveng::Vec2& b,
        const aveng::Vec2& c) noexcept
    {
        return (a + b + c) * (1.0f / 3.0f);
    }

    bool triangleOwnedByRectCentroid(
        const aveng::Triangle& t,
        const std::pmr::vector<aveng::Vec2>& pts,
        const aveng::Bounds2& ownershipRect) noexcept
    {
        const aveng::Vec2 a = pts[t.A];
        const aveng::Vec2 b = pts[t.B];
        const aveng::Vec2 c = pts[t.C];

        const aveng::Vec2 centroid = triangleCentroid(a, b, c);
        //
        //std::printf(
        //    "A=(%.3f, %.3f) B=(%.3f, %.3f) C=(%.3f, %.3f) Centroid=(%.3f, %.3f)\n",
        //    a.x, a.y,
        //    b.x, b.y,
        //    c.x, c.y,
        //    centroid.x, centroid.y
        //);

        bool result = pointInRectHalfOpen(centroid, ownershipRect);
        // std::printf("Result <Bool>: %d\n", result);
        return result;
    }

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

    // Pack a uint32_t site index into a float preserving bit pattern (GPU reads as uvec3)
    glm::vec3 packTriIndices(uint32_t a, uint32_t b, uint32_t c) {
        glm::vec3 v;
        // I'm not sure if this is bit-casting, I need to study what that implies.
        // All I know is that we can't return v.x directly due to being floats 
        // which means something different to the call-site
        std::memcpy(&v.x, &a, sizeof(uint32_t));
        std::memcpy(&v.y, &b, sizeof(uint32_t));
        std::memcpy(&v.z, &c, sizeof(uint32_t));
        return v;
    }

    void unpackTriIndices(
        const glm::vec3& tri,
        uint32_t& a,
        uint32_t& b,
        uint32_t& c) noexcept
    {
        std::memcpy(&a, &tri.x, sizeof(uint32_t));
        std::memcpy(&b, &tri.y, sizeof(uint32_t));
        std::memcpy(&c, &tri.z, sizeof(uint32_t));
    }

    aveng::Bounds2 expandBounds(aveng::Bounds2 b, float halo) noexcept {
        b.minX -= halo; b.minZ -= halo;
        b.maxX += halo; b.maxZ += halo;
        return b;
    }

    bool inBoundsInclusiveMax(const aveng::Bounds2& b, float x, float z) noexcept {
        // Use inclusive max to be robust against FP jitter on borders.
        return x >= b.minX && x <= b.maxX && z >= b.minZ && z <= b.maxZ;
    }

}

namespace aveng {

#ifdef M_DEBUG

	// Height data writer for debugging
    void dumpChunkHeightData(ChunkCoord coord, std::span<float> data)
    {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("heights_chunk_{}.txt", name);

        Debug::writeHeightDataToFile(fullPath, data);
    }

    void dumpTriangulationDatas(ChunkCoord coord, Triangulation* tri_data) {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("triangulation_chunk_{}.txt", name);

		Debug::writeTriangulationDataToFile(fullPath, tri_data);

    }

    void dumpSpatialGridData(ChunkCoord coord, const SpatialGrid* grid) {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("sgrid_chunk_{}.txt", name);

        Debug::writeSgridDataToFile(fullPath, grid);

    }

    void dumpHydraulicData(ChunkCoord coord, const procgen::ErosionWorkingSet* ws) {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("hydro_chunk_{}.txt", name);

        Debug::writeHydroDataToFile(fullPath, ws->delta);

    }

    void dumpThermalData(ChunkCoord coord, const procgen::ErosionWorkingSet* ws) {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("hydro_chunk_{}.txt", name);

        Debug::writeThermalDataToFile(fullPath, ws->delta);

    }

    // Height data writer for debugging
    void dumpChunkFinalHeightData(ChunkCoord coord, std::span<float> data)
    {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("FinalHeights_chunk_{}.txt", name);

        Debug::writeFinalHeightDataToFile(fullPath, data);
    }

#endif

    struct RecordPin {
        ChunkManager* mgr{};
        ChunkRecord* rec{};

        RecordPin() = default;

        RecordPin(ChunkManager& m, ChunkRecord* r, uint64_t frameIndex)
            : mgr(&m), rec(r)
        {
            // std::printf("Pinning chunk record for ChunkCoord(%d, %d)\n", r->coord.x, r->coord.z);
            mgr->pin(rec, frameIndex); // increments + touches
        }

        RecordPin(ChunkManager& m, ChunkRecord* r)
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

    ChunkManager::ChunkManager(ThreadPoolTaskSystem& tasks)
        : tasks_(tasks)
    {
        cfg_ = defaultTerrainConfig(); // Global Config
        cfg_.noise = defaultNoiseParams();
    }

    /* Manager Setups */
    void ChunkManager::initManagers(procgen::ErosionManager* er)
    {
        // So far we only have an ErosionManager
        erosionMgr_ = er;
        initManagerDefaults();
    }

    void ChunkManager::initManagerDefaults()
    {
        // nThreads is required for init.
        if (!erosionMgr_->switchToDefaultSettings(cfg_.nThreads)) {
            // User Error... you probably didn't init TerrainConfig properly.
        }
    }

    void ChunkManager::setErosionParameters(ErosionSettings eroCfg)
    {
        erosionMgr_->setWeatheringConfig(eroCfg);
    }

    /* 
     * All Points Ready means any overlapping regions can safely generate in parallel 
     * without the need to add more constly synchronization to keep the scheduler happy.
     * Note: this is a working hypothesis due to a deadlocking scenario :)
     * This readiness indication can belong to any stage. I'm just being optimistic
     * by giving it to the (early) requestAllPoints stage.
     * 
     * If we tighten up streaming policy we could maybe factor out the mutex
     * but that's beyond micro-optimizing at the moment.
     */
    bool ChunkManager::isAllPointsReady(const ChunkCoord coord) const {
        std::lock_guard<std::mutex> lock(allPointsReadyMut_);
        return allPointsReady_.find(coord) != allPointsReady_.end();
    }

    void ChunkManager::markAllPointsReady(ChunkCoord coord) {
        std::lock_guard<std::mutex> lock(allPointsReadyMut_);
        allPointsReady_.insert(coord);
    }

    void ChunkManager::clearAllPointsReady(ChunkCoord coord) {
        std::lock_guard<std::mutex> lock(allPointsReadyMut_);
        allPointsReady_.erase(coord);
    }

    //
    ChunkRecord* ChunkManager::getOrCreateRecord(ChunkCoord coord)
    {
        //std::printf("%s [%d, %d]\n", __FUNCTION__, coord.x, coord.z);
        const size_t hash = ChunkCoordHash{}(coord); // turns (x,z) into a size_t
        // Note: This only looks at the lowest 6 bits of the final hash. We use the MurmurHash3 algorithm for this.
        // const size_t stripeIdx = hash % STRIPES; // Determine which bucket's map the record ends up in - index will always be [0, STRIPES)
#ifdef MIDNIGHT_WYHASH
        const size_t stripeIdx = stripeIndexwh(coord);
#else
        const size_t stripeIdx = hash & (STRIPES - 1); // Use bitwise AND to get the lowest 6 bits - faster than % but only works if STRIPES is a power of two (it is)
#endif
        auto& bucket = records_[stripeIdx];

        // Nifty RAII lock_guard
        std::lock_guard<std::mutex> lock(bucket.mut);

        // Insert the key if it's missing, with a null unique_ptr placeholder.
        auto [it, inserted] = bucket.map.try_emplace(coord, nullptr);
        if (!inserted) {
            // std::printf("%s Record Already Created for (%d, %d)...\n", __FUNCTION__, coord.x, coord.z);
            return it->second.get();
        }

        // Create a new record - still holding the lock
        auto rec = std::make_unique<ChunkRecord>();
        rec->coord = coord;
        rec->halo = cfg_.halo;
        rec->coreBounds = { // Bounds are in world space with local space {0,0} at bottom-left
            coord.x * cfg_.chunkSize,
            coord.z * cfg_.chunkSize,
            (coord.x + 1) * cfg_.chunkSize,
            (coord.z + 1) * cfg_.chunkSize
        };

        // Only reserve FINAL here. Scratch is thread-local now. 2MB though...
        rec->final.reserve(2 * 1024 * 1024);
		rec->scratch.reserve(2 * 1024 * 1024);

        ChunkRecord* out = rec.get();
        it->second = std::move(rec); // "overwrite" the nullptr with the new record
        return out;
    }

    // Consider this implementation now that we've reasoned about pinning/unpinning, 
    std::shared_future</*FinalMeshCPU const**/bool> ChunkManager::requestMesh(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->meshOnce, [this, rec, frameIndex] {
            rec->meshF = tasks_.submit([this, rec, frameIndex]() -> bool /*FinalMeshCPU const**/ {

                RecordPin pipelineHold(*this, rec); // holds across all stages

                auto er = tasks_.wait(requestErosion(rec->coord, frameIndex));

                return true;// buildMesh(*rec);
            });
        });

        // [!] On the main thread we would use .get() on this future, which blocks.
        return rec->meshF;
    }

    std::shared_future<Points const*> ChunkManager::requestPoints(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        // std::printf("Request Points\n");
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->pointsOnce, [this, rec] {
            rec->pointsF = tasks_.submit([this, rec]() -> Points const* {
                RecordPin taskHold(*this, rec);   // Note: "neighbor sensitive" stages should pin their neighboring chunks.
                return buildPoints(*rec);
            });
        });

        return rec->pointsF;
    }

    // AllPoints (depends on 9 point sets)
    std::shared_future<AllPoints const*> ChunkManager::requestAllPoints(ChunkCoord c, uint64_t frameIndex)
    {
        // std::printf("[%s] ChunkCoord{%d,%d} \n", __FUNCTION__, c.x, c.z);
        ChunkRecord* rec = getOrCreateRecord(c);
        // std::printf("Request AllPoints\n");
        //auto pointsF = requestPoints(c, frameIndex);

        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->allPointsOnce, [this, rec, /*pointsF,  */c, frameIndex] {
            rec->allPointsF = tasks_.submit([this, rec, /*pointsF,*/ c, frameIndex]() -> AllPoints const* {
                // Keep the center record alive while we build its AllPoints
                RecordPin selfHold(*this, rec, frameIndex);

                // auto* points = tasks_.wait(pointsF);

                // 3x3 neighborhood around c
                std::array<ChunkCoord, 9> neighbors;
                get3x3Neighborhood(c, neighbors.data());

                // Pin neighbors for the duration so their arena-backed Points can't be evicted mid-build.
                std::array<RecordPin, 9> neighborHolds;
                for (int i = 0; i < 9; ++i) {
                    ChunkRecord* nrec = getOrCreateRecord(neighbors[i]);
                    neighborHolds[i] = RecordPin(*this, nrec, frameIndex);
                }

                // Ensure neighbors' points exist
                std::array<std::shared_future<Points const*>, 9> pf;
                for (int i = 0; i < 9; ++i) {
                    // // std::printf("Requesting points for neighbor: %d\n", i);
                    pf[i] = requestPoints(neighbors[i], frameIndex); // This is safe bc requestPoints uses call_once with that stage's once-flag
                }

                // Helping wait: keep worker productive
                for (int i = 0; i < 9; ++i) {
                    (void)tasks_.wait(pf[i]);
                }

                return buildAllPoints(*rec);
            });
        });

        return rec->allPointsF;
    }

    // Heights - We do not need the halo-region in order to compute heights
    // The height function will remain deterministic across world-space.
    std::shared_future<HeightField const*> ChunkManager::requestHeights(ChunkCoord c, uint64_t frameIndex) {
        auto rec = getOrCreateRecord(c);
        // std::printf("Request Heights\n");

        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->heightsOnce, [this, rec, frameIndex] {
            rec->heightsF = tasks_.submit([this, rec, frameIndex]() -> HeightField const* {
                RecordPin taskHold(*this, rec); 
                auto allPointF = requestAllPoints(rec->coord, frameIndex);
                auto* allP = tasks_.wait(allPointF);
                return buildHeights(*rec);
            });
        });

        return rec->heightsF;
    }

    // Triangulation
    std::shared_future<Triangulation const*> ChunkManager::requestTriangulation(ChunkCoord c, uint64_t frameIndex) {
        auto rec = getOrCreateRecord(c);
        // std::printf("Request Triangulation\n");

        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        // Captured values are cheap - `this` is safe due to strict lifetime rules. Review them
        std::call_once(rec->triangOnce, [this, rec, frameIndex] {
            rec->triangF = tasks_.submit([this, rec, frameIndex]() -> Triangulation const* {
                RecordPin taskHold(*this, rec);   // RAII pin
                auto heightF = requestHeights(rec->coord, frameIndex);
                auto* heights = tasks_.wait(heightF);
                return buildTriangulation(*rec);
            });
        });

        return rec->triangF;
    }

    std::shared_future<SpatialGrid const*> ChunkManager::requestSpatialGrid(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        // std::printf("Request SpatialGrid\n");

        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->spatialOnce, [this, rec, frameIndex] {
            rec->spatialF = tasks_.submit([this, rec, frameIndex]() -> SpatialGrid const* {
                RecordPin pin(*this, rec);
                auto triangF = requestTriangulation(rec->coord, frameIndex);
                auto* tri = tasks_.wait(triangF);
                return buildSpatialGrid(*rec);
            });
        });

        return rec->spatialF;
    }

    // Erosion
    std::shared_future<ErosionField const*> ChunkManager::requestErosion(ChunkCoord c, uint64_t frameIndex) {
        auto rec = getOrCreateRecord(c);
        // std::printf("Request Erosion\n");

        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->erosionOnce, [this, rec, frameIndex] {

            // Get settings
            const ErosionSettings s = erosionMgr_ ? erosionMgr_->getActiveSettings() : ErosionSettings{};

            // We could, of course, define these internal lambdas beforehand
            rec->erosionF = tasks_.submit([this, rec, s, frameIndex]() -> ErosionField const* {
                RecordPin taskHold(*this, rec);
                auto spatialF = requestSpatialGrid(rec->coord, frameIndex);
                auto* spatial = tasks_.wait(spatialF);
                return buildErosion(*rec, s);
            });

        });

        return rec->erosionF;
    }

    Points const* ChunkManager::buildPoints(ChunkRecord& rec)
    {
        // std::printf("Build Points\n");

        // 1) Reset thread-local scratch for this job
        tlsScratchArena().reset();

        auto* mr = tlsScratchArena().mr();
        assert(mr && "tlsScratch.mr() is null");

        // 2) Generate the chunk seed - It's used across stages 
        // (and mixed for stochastic determinism. Cool, right?)
        rec.chunkSeed = chunkSeed(cfg_.worldSeed, rec.coord);

        /* [IMPORTANT]
         *   Are heights deterministic across chunks?
         *   They should be, if you compute heights as a pure function of world position and global params:
         */

        // 3) Generate blue noise points using thread-local scratch
        noise::BlueNoiseConfig bnCfg{};
        bnCfg.MinDist = cfg_.minPointDist; 
        bnCfg.MaxTries = 30;

        auto candidates = GenerateBlueNoiseSeeded(
            rec.chunkSeed,
            rec.coreBounds.minX,
            rec.coreBounds.minZ,
            rec.coreBounds.maxX,
            rec.coreBounds.maxZ,
            bnCfg,
            tlsScratchArena().mr()  // Use thread-local scratch
#ifdef M_DEBUG
            , rec.coord // instead of this, just pass the file path
#endif
        );

        // 4) Allocate the published product in FINAL memory.
        //    This is the pointer that the future will return, so it must outlive scratch.
        if (!rec.points) {
            auto alloc = std::pmr::polymorphic_allocator<Points>(rec.final.mr());
            rec.points = alloc.allocate(1);
            std::construct_at(rec.points, rec.final.mr()); // Points(mr)
        }

        // 5) Copy candidates from thread-local scratch to final arena
        //    Blue noise generation is complete - just moving to persistent storage
        rec.points->core.clear();
        rec.points->core.reserve(candidates.size());
        rec.points->core.insert(rec.points->core.end(), candidates.begin(), candidates.end());

        // 6) Done. Scratch can be reused immediately by this worker for the next job.
        return rec.points;
    }

    AllPoints const* ChunkManager::buildAllPoints(ChunkRecord& rec) {
        // std::printf("Build All Points\n");
        // 1) Reset thread-local scratch for this job
        tlsScratchArena().reset();
        
        // 2) Allocate AllPoints struct in chunk scratch (persists across stages)
        if (!rec.allPoints) {
            auto alloc = std::pmr::polymorphic_allocator<AllPoints>(rec.scratch.mr());
            rec.allPoints = alloc.allocate(1);
            std::construct_at(rec.allPoints, rec.scratch.mr());
        }
        
        // 3) Calculate expanded bounds for halo region
        const Bounds2 haloBounds = expandBounds(rec.coreBounds, rec.halo);
        
        // 4) Temporary collection using thread-local scratch
        std::pmr::vector<Vec2> collected(tlsScratchArena().mr());
        collected.reserve(12000); // heuristic: ~1000 points/chunk x 9 neighbors
        
        // 5) Iterate through 9 neighbors and collect points within halo
        std::array<ChunkCoord, 9> neighbors;
        get3x3Neighborhood(rec.coord, neighbors.data());
        
        for (int i = 0; i < 9; ++i) {
            ChunkRecord* nrec = getOrCreateRecord(neighbors[i]);
            
            // Points should exist (pinned and requested in requestAllPoints)
            if (!nrec->points) {
                continue; // defensive
            } 
            
            // Filter points within halo bounds
            for (const auto& pt : nrec->points->core) {
                if (inBoundsInclusiveMax(haloBounds, pt.x, pt.y)) {
                    collected.push_back(pt);
                }
            }
        }
        
        // We grab points from generated neighbors, we don't generate halo points for the coord.
        // However, it's still nice to dedupe
        // 6) Deduplicate using quantization - This can actually be improved upon!
        constexpr float DEDUPE_EPS = 1e-4f; // smaller than minPointDist - we might be able to optimize further if the our algo's are deterministic (they are...)
        // std::pmr::unordered_set<QKey, QKeyHash> seen(tlsScratch.mr());
        std::pmr::unordered_set<QKey, QKeyHash> seen(
            0, QKeyHash{}, std::equal_to<QKey>{}, tlsScratchArena().mr()
        );
        seen.reserve(collected.size());
        
        std::pmr::vector<Vec2> unique(tlsScratchArena().mr());
        unique.reserve(collected.size());
        
        for (const auto& pt : collected) {
            QKey key = quantizeFast(pt, DEDUPE_EPS);
            if (seen.insert(key).second) {
                unique.push_back(pt);
            }
        }
        
        // 7) Identify which points are in core region
        rec.allPoints->pts.clear();
        rec.allPoints->pts.reserve(unique.size());
        rec.allPoints->coreIdx.clear();
        rec.allPoints->coreIdx.reserve(unique.size() / 9); // ~1/9 are core
        
        for (size_t i = 0; i < unique.size(); ++i) {
            const auto& pt = unique[i];
            rec.allPoints->pts.push_back(pt);
            
            // Check if point is in core bounds (not just halo)
            if (rec.coreBounds.contains(pt.x, pt.y)) {
                rec.allPoints->coreIdx.push_back(static_cast<uint32_t>(i));
            }
        }
        
        // 8) Done
        return rec.allPoints;
    }

    HeightField const* ChunkManager::buildHeights(ChunkRecord& rec)
    {
        // std::printf("Build Heights\n");
        // 1) Reset thread-local scratch for this job
        tlsScratchArena().reset();

        // Scratch setup
        auto* mr = tlsScratchArena().mr();
        assert(mr && "tlsScratch.mr() is null");

        std::pmr::vector<float> heightsOut(mr);
        heightsOut.resize(rec.allPoints->pts.size());

        // Bring the (default) noise
        noise::NoiseParams np = cfg_.noise;

        for (size_t i = 0; i < rec.allPoints->pts.size(); i++) {
            // Heights in parallel with points (Sites)
            heightsOut[i] = FractalNoiseV2(
                rec.allPoints->pts[i].x,
                rec.allPoints->pts[i].y, // Z - in engine terms
                np);
        }

#ifdef M_DEBUG
		//dumpChunkHeightData(rec.coord, heightsOut);
#endif

        // 4) Allocate the published product in FINAL memory.
        //    This is the pointer that the future will return, so it must outlive scratch.
        if (!rec.heightField) {
            auto alloc = std::pmr::polymorphic_allocator<HeightField>(rec.final.mr());
            rec.heightField = alloc.allocate(1);
            std::construct_at(rec.heightField, rec.final.mr()); // Points(mr)
        }

        // 5) Copy candidates from thread-local scratch to final arena
        //    Blue noise generation is complete - just moving to persistent storage
        rec.heightField->heights.clear();
        rec.heightField->heights.reserve(heightsOut.size());
        rec.heightField->heights.insert(rec.heightField->heights.end(), heightsOut.begin(), heightsOut.end());

        // 6) Done. Scratch can be reused immediately by this worker for the next job.
        return rec.heightField;

    }

    Triangulation const* ChunkManager::buildTriangulation(ChunkRecord& rec)
    {
        // std::printf("Build Triangulation\n");
        // Note, I haven't designed any configurations for Triangulation. Potential options:
        // - omit the circumcenter calculations. They're only needed if we'd like a voronoi layer, but enabled by default.
        // - add different triangulation algorithms for better quality meshes, but maybe a perf tradeoff.

        // Prereq: allPoints must exist (caller discipline or assert/hard dependency)
        assert(rec.allPoints && "Triangulation requires AllPoints");

        // 1) Reset thread-local scratch for this job
        tlsScratchArena().reset();
        auto* scratchMr = tlsScratchArena().mr();
        assert(scratchMr && "tlsScratch.mr() is null");

        auto* finalMr = rec.final.mr();
        assert(finalMr && "rec.final.mr() is null");

        // 2) Vertex positions: typically triangulate ALL points (core + halo) for continuity.
        // If rec.allPoints->pts is already Vec2, great.
        std::span<const Vec2> vertexPos = rec.allPoints->pts;
        const SiteIndex vertexCount = static_cast<SiteIndex>(vertexPos.size());

        // 3) Bowyer–Watson: allocates Triangulation in FINAL arena and fills only `tris`
        Triangulation* tri = TriangulateBowyerWatson(vertexPos, scratchMr, finalMr);

        // 4) Publish/adopt as the chunk product
        rec.triangulation = tri;

        // Ensure non-tris outputs start clean (defensive, cheap, but 
        // also necessary when we eventually allow re-triangulation for mesh updates)
        tri->halfEdges.clear();
        tri->cache.clear();
        tri->circumcenters.clear();
        tri->triEdge0.clear();
        tri->siteEdge.clear();

        // Build half-edge mesh and accelerators into `tri`
        BuildHalfEdgeMesh(
            vertexPos,
            *tri,   // output parameter
            vertexCount,
            scratchMr
        );

#ifdef M_DEBUG
        // Optional: sanity invariants (adapt to your exact half-edge layout)
        // assert(tri->triEdge0.size() == tri->tris.size());
        // assert(tri->siteEdge.size() == static_cast<size_t>(vertexCount));
        // If you do 3 half-edges per triangle:
        // assert(tri->halfEdges.size() == tri->tris.size() * 3);

        //dumpTriangulationDatas(rec.coord, rec.triangulation);
#endif

        // Triangle ownership


        return rec.triangulation;
    }

    SpatialGrid const* ChunkManager::buildSpatialGrid(ChunkRecord& rec)
    {
        // std::printf("Build SpatialGrid\n");

        // If we ever get called redundantly (shouldn't happen with call_once),
        // just return the already-published product.
        if (rec.spatial.has_value()) {
            std::printf("SpatialGrid Already has value?\n");
            return &(*rec.spatial);
        }

        // Expand core bounds by halo for spatial queries that need neighbor continuity.
        const float minX = rec.coreBounds.minX - rec.halo;
        const float minZ = rec.coreBounds.minZ - rec.halo;
        const float maxX = rec.coreBounds.maxX + rec.halo;
        const float maxZ = rec.coreBounds.maxZ + rec.halo;

        // Cell size: prototype used ~MinPointDist for good performance.
        const float cellSize = cfg_.minPointDist;

#ifdef M_DEBUG
        //{
        //    // Checking out totals from previous stages
        //    if (rec.triangulation) {
        //        std::printf("[SpatialGrid] tri: tris=%zu halfEdges=%zu triEdge0=%zu siteEdge=%zu\n",
        //            rec.triangulation->tris.size(),
        //            rec.triangulation->halfEdges.size(),
        //            rec.triangulation->triEdge0.size(),
        //            rec.triangulation->siteEdge.size());
        //    }
        //    if (rec.allPoints) {
        //        std::printf("[SpatialGrid] pts: pts=%zu coreIdx=%zu\n",
        //            rec.allPoints->pts.size(),
        //            rec.allPoints->coreIdx.size());
        //    }
        //    if (rec.heightField) {
        //        std::printf("[SpatialGrid] hf: heights=%zu\n",
        //            rec.heightField->heights.size());
        //    }

        //    // Proof that the SpatialGrid covers the halo region for individual chunks.
        //    // This means we can include reliable adjacency information, efficiently, to compute shaders
        //    std::printf("[SpatialGrid] cellSize=%f bounds(minx,minz,maxx,maxz)=(%f,%f,%f,%f) halo=%f\n",
        //        cellSize, minX, minZ, maxX, maxZ, rec.halo);

        //    if (!(cellSize > 0.0f)) std::printf("[SpatialGrid][!!] cellSize is not > 0\n");
        //    if (!(maxX > minX && maxZ > minZ)) std::printf("[SpatialGrid][!!] bounds are degenerate/inverted\n");
        //}
#endif
        // Build via free function (returns owning pointer)
        std::unique_ptr<SpatialGrid> sg = BuildSpatialGrid(
            rec.triangulation,
            rec.allPoints,
            rec.heightField,
            cellSize,
            minX, minZ,
            maxX, maxZ
        );

        // Publish into the record
        rec.spatial.emplace(std::move(*sg));

#ifdef M_DEBUG

        //if (!sg) {
        //    std::printf("[SpatialGrid][!!] BuildSpatialGrid returned nullptr\n");
        //    // If you want to hard-fail but still see the message:
        //    assert(false && "BuildSpatialGrid returned null (see console prereq dump above)");
        //}

        //assert(sg && "[buildSpatialGrid] BuildSpatialGrid returned null");
        // std::printf("Completed SpatialGrid {%d, %d}\n", rec.coord.x, rec.coord.z);
        //dumpSpatialGridData(rec.coord, &(*rec.spatial));
#endif
        markAllPointsReady(rec.coord);
        // Return stable pointer into the optional
        return &(*rec.spatial);

    }

    ErosionField const* ChunkManager::buildErosion(ChunkRecord& rec, const ErosionSettings& settings)
    {
        // std::printf("Build Erosion\n");
        // Erosion is call_once'd => safe to reuse rec.scratch for the whole stage
        rec.scratch.reset(); // whatever your API is

        auto* scratchMr = rec.scratch.mr();
        auto* finalMr = rec.final.mr();

        // 1) Construct working buffers in scratch
        procgen::ErosionWorkingSet ws(scratchMr);

		// Seeds for any stochastic components in each stage
        // ensures deterministic output per chunk, but different patterns across stages.
        // TODO: move them elsewhere so they're not computed when the corresponding stage is disabled.
        const uint64_t hardnessSeed = wyhash64(rec.chunkSeed, SeedTag::Hardness);
        const uint64_t hydroSeed    = wyhash64(rec.chunkSeed, SeedTag::Hydraulic);
		const uint64_t thermalSeed  = wyhash64(rec.chunkSeed, SeedTag::Thermal);
		const uint64_t ridgeSeed    = wyhash64(rec.chunkSeed, SeedTag::Ridge);
		const uint64_t smoothSeed   = wyhash64(rec.chunkSeed, SeedTag::Smooth);

        // Convenient way to retrieve heights for copying
        // const auto* hf = requestHeights(rec.coord, /*frame*/0).get(); 
        // Use this approach (above) if we ever need to guarantee completion of all prerequisite stages.
        // However, in our architecture we've synchronized stages so this isn't necessary.

        // Or do something like the below snippet, to be safe, should we change our design.
        // (void)requestHeights(rec.coord, 0 /*frameIndex*/).get(); // ensure built
        // (void)requestAllPoints(rec.coord, 0 /*frameIndex*/).get(); // ensure built
        //auto const& heights = rec.heightField->heights;

        // Get heights for copying.
        auto const& heights = rec.heightField->heights;
        const size_t N = heights.size();

        // Resize to num points we're working on
        ws.workHeights.resize(N);
        ws.delta.resize(N);
        ws.hardness.resize(N);

        // Init working heights and deltas
        std::copy(heights.begin(), heights.end(), ws.workHeights.begin());
        std::fill(ws.delta.begin(), ws.delta.end(), 0.0f);

        /*
        * Signatures get a little messy here.
        * Haste be with us.
        */

        if (settings.hardnessMapEnabled) {
        
            // 2) Hardness in scratch
            procgen::ComputeHardnessMap(
                ws.hardness, 
			    rec.allPoints->pts, // INVARIANT REMINDER - allPoints->pts.size() == heightField->heights.size()
                ws.workHeights,
                settings.hardness,
                hardnessSeed
            );

        }

        if (settings.hydraulicErosionEnabled) {
        
            // 3) Hydraulic pass writes ws.delta, then apply into ws.workHeights
            procgen::ComputeHydraulicErosion(
                ws, 
                *rec.allPoints, 
                *rec.triangulation, 
                rec.spatial.value(), 
                settings.hydraulic,
                hydroSeed,
                tasks_
            );

            ApplyDelta(ws.workHeights, ws.delta);
            std::fill(ws.delta.begin(), ws.delta.end(), 0.0f);
        }

#ifdef M_DEBUG
       // std::printf("Completed HydroErosion {%d, %d}\n", rec.coord.x, rec.coord.z);
       //dumpHydraulicData(rec.coord, &ws);
#endif


        if (settings.thermalErosionEnabled) {
        
            // 4) Thermal pass writes ws.delta, then apply
            procgen::ComputeThermalErosion(
                ws,  
                *rec.allPoints,
                *rec.triangulation,
                rec.spatial.value(),
                settings.thermal,
                thermalSeed,
                tasks_
            );

            ApplyDelta(ws.workHeights, ws.delta);
            std::fill(ws.delta.begin(), ws.delta.end(), 0.0f);
        }


        if (settings.ridgeEnhancementEnabled) {

            // 5) Ridge enhancement can use ping-pong
            ws.ping.resize(N);
            ComputeRidgeEnhancement( /* WARNING: I DID NOT REVIEW THIS CODE AT ALL */
                ws,
                *rec.allPoints,
                *rec.triangulation,
                rec.spatial.value(),
                settings.ridges,
                ridgeSeed,
                tasks_
            );

            ws.workHeights.swap(ws.ping);
        }

        // Allocate the published product in FINAL memory
        // This is the pointer that the future will return
        if (!rec.erosion) {
            auto alloc = std::pmr::polymorphic_allocator<ErosionField>(rec.final.mr());
            rec.erosion = alloc.allocate(1);
            std::construct_at(rec.erosion, rec.final.mr()); // Points(mr)
        }

        // 5) Copy candidates from thread-local scratch to final arena
		//    eHeights are the completed heights with erosion deltas applied, ready for mesh generation.
        rec.erosion->eHeights.clear();
        rec.erosion->eHeights.reserve(ws.workHeights.size());
        rec.erosion->eHeights.insert(rec.erosion->eHeights.end(), ws.workHeights.begin(), ws.workHeights.end());

#ifdef M_DEBUG
        std::printf("[Chunk Completed] {%d, %d}\n", rec.coord.x, rec.coord.z);
        //dumpChunkFinalHeightData(rec.coord, rec.erosion->eHeights);
#endif

        // 6) Done. Scratch can be reused immediately by this worker for the next job.
        return rec.erosion;

    }

    FinalMeshCPU const* ChunkManager::buildMesh(ChunkRecord& rec)
    {
        if( rec.points == nullptr 
         || rec.allPoints == nullptr
         || rec.heightField == nullptr
         || rec.triangulation == nullptr
         || rec.erosion == nullptr
        ) { return nullptr; }

        const auto& pts       = rec.allPoints->pts;
        const auto& coreIdx   = rec.allPoints->coreIdx;
        const auto& triangles = rec.triangulation->tris;
        const auto& eHeights  = rec.erosion->eHeights;
        const size_t totalPts = pts.size();
        const size_t numCoreVerts = coreIdx.size();

        // Allocate FinalMeshCPU in durable (final) arena
        if (!rec.finalMesh) {
            auto alloc = std::pmr::polymorphic_allocator<FinalMeshCPU>(rec.final.mr());
            rec.finalMesh = alloc.allocate(1);
            std::construct_at(rec.finalMesh, rec.final.mr());
        }
        FinalMeshCPU& mesh = *rec.finalMesh;

        // -- Core membership mask (flat, no hashing) --
        std::pmr::vector<uint8_t> isCore(totalPts, uint8_t(0), tlsScratchArena().mr());
        for (uint32_t ci : coreIdx) {
            // for 4-5k points this is trivially expensive
            // but there should be a faster way to fill a vector with 1's
            isCore[ci] = 1;
        }

        // -- old2new remap: global site index -> core-only VBO index --
        constexpr uint32_t UNMAPPED = std::numeric_limits<uint32_t>::max();
        std::pmr::vector<uint32_t> old2new(
            totalPts, 
            UNMAPPED,
            tlsScratchArena().mr()
        );

        // 1a. VBO positions (core only)
        mesh.vbo_positions.clear();
        mesh.vbo_positions.reserve(numCoreVerts);
        for (uint32_t ci : coreIdx) {
            old2new[ci] = static_cast<uint32_t>(mesh.vbo_positions.size());
            mesh.vbo_positions.push_back(glm::vec3(pts[ci].x, -eHeights[ci], pts[ci].y));
        }

        // 1b/1c. IBO indices + tris (core triangles only)
        mesh.ibo_indices.clear();
        mesh.tris.clear();
        mesh.ibo_indices.reserve(triangles.size() * 3);
        mesh.tris.reserve(triangles.size());

        for (const auto& tri : triangles) {
            if (!(isCore[tri.A] && isCore[tri.B] && isCore[tri.C])) { continue; }

            mesh.ibo_indices.push_back(old2new[tri.A]);
            mesh.ibo_indices.push_back(old2new[tri.B]);
            mesh.ibo_indices.push_back(old2new[tri.C]);

            mesh.tris.push_back(packTriIndices(old2new[tri.A], old2new[tri.B], old2new[tri.C]));
        }

        // 1d. Packed positions: [core..., halo...]
        mesh.packed_positions.clear();
        mesh.packed_positions.reserve(totalPts);
        for (uint32_t ci : coreIdx) {
            mesh.packed_positions.push_back(glm::vec3(pts[ci].x, -eHeights[ci], pts[ci].y));
        }
        for (size_t i = 0; i < totalPts; ++i) {
            if (!isCore[i]) {
                mesh.packed_positions.push_back(glm::vec3(pts[i].x, -eHeights[i], pts[i].y));
            }
        }

        // 1e. Adjacency: one entry per vertex (core + halo), triangle indices are chunk-local
        mesh.adjacency.clear();
        mesh.adjacency.resize(totalPts);
        std::memset(mesh.adjacency.data(), 0, totalPts * sizeof(procgen::VertexAdjacency));

        for (uint32_t ti = 0; ti < static_cast<uint32_t>(triangles.size()); ++ti) {
            const SiteIndex verts[3] = { triangles[ti].A, triangles[ti].B, triangles[ti].C };
            for (int k = 0; k < 3; ++k) {
                auto& adj = mesh.adjacency[verts[k]];
                if (adj.count < procgen::MAX_ADJACENT_TRIS) {
                    adj.triangleIndices[adj.count++] = ti;
                }
            }
        }

        return rec.finalMesh;
    }

    /* Async */
    uint64_t ChunkManager::requestRenderableAsync(ChunkCoord center, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(center);

        uint64_t requestId = 0;
        bool shouldSubmit = false;

        {
            std::scoped_lock lock(rec->renderableMutex);

            // If already queued/building/ready, decide your policy.
            // Simplest policy: don't enqueue duplicate builds if something is already in flight or done.
            if (rec->renderableState == RenderableBuildState::Queued ||
                rec->renderableState == RenderableBuildState::Building ||
                rec->renderableState == RenderableBuildState::Ready)
            {
                return rec->requestedRenderableId;
            }

            requestId = ++rec->requestedRenderableId;
            rec->renderableState = RenderableBuildState::Queued;
            rec->renderablePublished = false;
            shouldSubmit = true;
        }
        if (shouldSubmit) {
            tasks_.submit([this, center, frameIndex, requestId]() {
                ChunkRecord* workerRec = getOrCreateRecord(center);
                RecordPin hold(*this, workerRec, frameIndex);

                {
                    std::scoped_lock lock(workerRec->renderableMutex);

                    // If another request superseded us before we even started, abandon.
                    if (workerRec->requestedRenderableId != requestId) {
                        return;
                    }

                    workerRec->renderableState = RenderableBuildState::Building;
                }

                try {
                    auto renderable = generate(center, frameIndex, requestId);

                    bool shouldPublish = false;

                    {
                        std::scoped_lock lock(workerRec->renderableMutex);

                        // If stale, discard result silently.
                        if (workerRec->requestedRenderableId != requestId) {
                            return;
                        }

                        workerRec->renderableResult = std::move(renderable);
                        workerRec->completedRenderableId = requestId;
                        workerRec->renderableState = RenderableBuildState::Ready;
                        workerRec->renderablePublished = true;
                        shouldPublish = true;
                    }

                    if (shouldPublish) {
                        completedRenderables_.push(procgen::RenderableCompletion{
                            .coord = center,
                            .requestId = requestId,
                            .success = true
                        });

                        // For High Perf streaming without chunk renderable cache
                        //completedRenderables_.push(procgen::RenderableCompletion_ALT{
                        //    .coord = center,
                        //    .requestId = requestId,
                        //    .success = true,
                        //    .renderable = std::move(renderable)
                        //});
                    }
                }
                catch (...) { // ugh
                    {
                        std::scoped_lock lock(workerRec->renderableMutex);

                        // Only mark failure if still current request
                        if (workerRec->requestedRenderableId == requestId) {
                            workerRec->renderableState = RenderableBuildState::Failed;
                        }
                    }

                    completedRenderables_.push(procgen::RenderableCompletion{
                        .coord = center,
                        .requestId = requestId,
                        .success = false
                    });
                }
            });
        }
    }

    // ---------------------------------------------------------------------------
    // Renderable assembly: 3x3 core region + 5x5 support (halo) region
    // ---------------------------------------------------------------------------


    /* meat n' potatoes */
    std::unique_ptr<procgen::TerrainRenderable>
        ChunkManager::generate(ChunkCoord center, uint64_t frameIndex, uint64_t requestId)
    {
        ChunkCoord neighbors[25];
        get5x5Neighborhood(center, neighbors);

        std::array<RecordPin, 25> pins;
        for (int i = 0; i < 25; ++i) {
            ChunkRecord* r = getOrCreateRecord(neighbors[i]);
            pins[i] = RecordPin(*this, r, frameIndex);
        }

        std::array<std::shared_future</*FinalMeshCPU const**/ bool>, 9> meshFutures;
        for (int i = 0; i < 9; ++i) {
            meshFutures[i] = requestMesh(neighbors[i], frameIndex);
        }

        std::array<std::shared_future<SpatialGrid const*>, 16> spatialFutures;
        for (int i = 9; i < 25; ++i) {
            spatialFutures[i - 9] = requestSpatialGrid(neighbors[i], frameIndex);
        }

        for (int i = 0; i < 9; ++i) {
            tasks_.wait(meshFutures[i]);
        }

        for (int i = 0; i < 16; ++i) {
            tasks_.wait(spatialFutures[i]);
        }

        return buildRenderablev2(center, frameIndex);
    }

    std::unique_ptr<procgen::TerrainRenderable> ChunkManager::buildRenderablev2(ChunkCoord center, uint64_t frameIndex)
    {
        using namespace procgen;

        ChunkCoord neighbors[25];
        get5x5Neighborhood(center, neighbors); // inner 3x3 = [0..8]

        std::array<ChunkRecord*, 25> recs{};
        for (int i = 0; i < 25; ++i) {
            recs[i] = getOrCreateRecord(neighbors[i]);
        }

        auto renderable = std::make_unique<TerrainRenderable>();
        renderable->center = center;

        const float cs = cfg_.chunkSize;
        // Compute world bounds for core 3x3 and support 5x5
        const glm::vec2 coreMin = { (center.x - 1) * cs, (center.z - 1) * cs };
        const glm::vec2 coreMax = { (center.x + 2) * cs, (center.z + 2) * cs };
        const glm::vec2 suppMin = { (center.x - 2) * cs, (center.z - 2) * cs };
        const glm::vec2 suppMax = { (center.x + 3) * cs, (center.z + 3) * cs };

        std::array<Bounds2, 25> ownershipRects{};
        for (int i = 0; i < 25; ++i) {
            const ChunkCoord cc = neighbors[i];

            ownershipRects[i] = Bounds2{
                cc.x * cs,
                cc.z * cs,
                (cc.x + 1) * cs,
                (cc.z + 1) * cs
            };
        }

        // Step 2 - Triangle ownership determines included vertices
        std::vector<TriRef> coreTriRefs;
        std::vector<TriRef> haloTriRefs;
        coreTriRefs.reserve(4096);
        haloTriRefs.reserve(4096);

        for (int i = 0; i < 25; ++i) {
            ChunkRecord* rec = recs[i];
            if (!rec || !rec->triangulation || !rec->allPoints) {
                continue;
            }

            //for (int i = 0; i < 25; ++i) {
            //    printf("neighbors[%d] = (%d,%d)\n", i, neighbors[i].x, neighbors[i].z);
            //}

            const auto& tris = rec->triangulation->tris;
            const auto& pts = rec->allPoints->pts;
            const Bounds2& ownerRect = ownershipRects[i];

            for (uint32_t triIdx = 0; triIdx < static_cast<uint32_t>(tris.size()); ++triIdx) {
                const auto& t = tris[triIdx];

                assert(t.A < pts.size() && t.B < pts.size() && t.C < pts.size());

                if (!triangleOwnedByRectCentroid(t, pts, ownerRect)) {
                    continue;
                }

                TriRef ref{};
                ref.rec = rec;
                ref.localTriIndex = triIdx;
                ref.neighborIdx = static_cast<uint8_t>(i);

                if (i < 9) {
                    coreTriRefs.push_back(ref);
                }
                else {
                    haloTriRefs.push_back(ref);
                }

            }
        }

        // Step 3 - Assign global vertex indices
        constexpr uint32_t UNMAPPED = std::numeric_limits<uint32_t>::max();

        std::vector<std::vector<uint32_t>> old2global(25);
        for (int i = 0; i < 25; ++i) {
            if (!recs[i] || !recs[i]->allPoints) {
                continue;
            }

            const size_t nPts = recs[i]->allPoints->pts.size();
            old2global[i].assign(nPts, UNMAPPED);
        }

        // Lazy vertex global index assignment helper
        // This is where our packed set becomes minimal.
        // We only reference vertices given owned triangles
        auto mapVertex = [&](int neighborIdx, uint32_t localSiteIdx) -> uint32_t
        {
            uint32_t& slot = old2global[neighborIdx][localSiteIdx];
            if (slot != UNMAPPED) {
                return slot;
            }

            ChunkRecord* rec = recs[neighborIdx];
            const auto& pts = rec->allPoints->pts;
            const auto& heights =
                (rec->erosion) ? rec->erosion->eHeights
                : rec->heightField->heights;

            const uint32_t gIdx = static_cast<uint32_t>(renderable->packedPositions.size());
            slot = gIdx;

            renderable->packedPositions.emplace_back(
                pts[localSiteIdx].x,
                -heights[localSiteIdx], // Note, we're inverting Y (TODO: This is being done elsewhere, I think this is the place to do it though)
                pts[localSiteIdx].y,
                1.0f
            );

            return gIdx;
        };

        // Step 4 - Emit packed triangles [core | halo] and build IBO
        uint32_t totalCoreTris = static_cast<uint32_t>(coreTriRefs.size());
        uint32_t totalHaloTris = static_cast<uint32_t>(haloTriRefs.size());

        renderable->packedTriangles.resize(totalCoreTris + totalHaloTris);
        renderable->ibo.reserve(totalCoreTris * 3);

        uint32_t coreWrite = 0;
        uint32_t haloWrite = totalCoreTris;

        for (const TriRef& ref : coreTriRefs) {
            const auto& t = ref.rec->triangulation->tris[ref.localTriIndex];

            const uint32_t gA = mapVertex(ref.neighborIdx, t.A);
            const uint32_t gB = mapVertex(ref.neighborIdx, t.B);
            const uint32_t gC = mapVertex(ref.neighborIdx, t.C);

            renderable->packedTriangles[coreWrite++] = packTriIndices(gA, gB, gC);
            renderable->ibo.push_back(gA);
            renderable->ibo.push_back(gB);
            renderable->ibo.push_back(gC);
        }

        const uint32_t totalCoreVerts = static_cast<uint32_t>(renderable->packedPositions.size());

#ifdef M_DEBUG
        //for (uint32_t i = 0; i < static_cast<uint32_t>(renderable->ibo.size()); ++i) {
        //    if (renderable->ibo[i] >= totalCoreVerts) {
        //        printf("BAD CORE IBO[%u] = %u, totalCoreVerts=%u\n",
        //            i, renderable->ibo[i], totalCoreVerts);
        //    }
        //    assert(renderable->ibo[i] < totalCoreVerts);
        //}
#endif

        for (const TriRef& ref : haloTriRefs) {
            const auto& t = ref.rec->triangulation->tris[ref.localTriIndex];

            const uint32_t gA = mapVertex(ref.neighborIdx, t.A);
            const uint32_t gB = mapVertex(ref.neighborIdx, t.B);
            const uint32_t gC = mapVertex(ref.neighborIdx, t.C);

            renderable->packedTriangles[haloWrite++] = packTriIndices(gA, gB, gC);
        }

        const uint32_t totalVerts = static_cast<uint32_t>(renderable->packedPositions.size());
        const uint32_t totalHaloVerts = totalVerts - totalCoreVerts;
        const uint32_t totalTris = totalCoreTris + totalHaloTris;

        // ----- Build adjacency [core verts | halo verts] -----
        renderable->packedAdjacency.resize(totalVerts);
        std::memset(
            renderable->packedAdjacency.data(),
            0,
            totalVerts * sizeof(VertexAdjacency)
        );

        for (uint32_t triIdx = 0; triIdx < totalTris; ++triIdx) {
            uint32_t a, b, c;
            unpackTriIndices(renderable->packedTriangles[triIdx], a, b, c);

            const uint32_t verts[3] = { a, b, c };
            for (int k = 0; k < 3; ++k) {
                auto& adj = renderable->packedAdjacency[verts[k]];
                if (adj.count < MAX_ADJACENT_TRIS) {
                    adj.triangleIndices[adj.count++] = triIdx;
                }
            }
        }

        // ----- Build VBO (3x3 core only) -----
        renderable->vbo.resize(totalCoreVerts);
        for (uint32_t v = 0; v < totalCoreVerts; ++v) {
            renderable->vbo[v] = glm::vec3(renderable->packedPositions[v]);
        }

        // ----- Fill alignment UBO -----
        auto& align = renderable->alignment;

        // Positions: [core | halo]
        align.baseCorePosition = 0;
        align.countCorePosition = totalCoreVerts;
        align.countHaloPosition = totalHaloVerts;

        // Triangles: [core | halo]
        align.baseCoreTriangle = 0;
        align.countCoreTriangle = totalCoreTris;
        align.countHaloTriangle = totalHaloTris;

        // Adjacency: [core | halo]
        align.baseCoreAdjacency = 0;
        align.countCoreAdjacency = totalCoreVerts;
        align.countHaloAdjacency = totalHaloVerts;

        align.coreMinXZ = coreMin;
        align.coreMaxXZ = coreMax;
        align.supportMinXZ = suppMin;
        align.supportMaxXZ = suppMax;
#if M_DEBUG
        //std::printf(
        //    "Renderable {%d,%d}: coreTris=%zu haloTris=%zu total=%zu\n",
        //    center.x, center.z,
        //    coreTriRefs.size(),
        //    haloTriRefs.size(),
        //    coreTriRefs.size() + haloTriRefs.size()
        //);

        //std::printf(
        //    "Renderable {%d,%d}: packedVerts=%zu\n",
        //    center.x, center.z,
        //    renderable->packedPositions.size()
        //);

        //printf("coreTris=%u haloTris=%u coreVerts=%u haloVerts=%u vbo=%zu ibo=%zu\n",
        //    totalCoreTris, totalHaloTris, totalCoreVerts, totalHaloVerts,
        //    renderable->vbo.size(), renderable->ibo.size());
#endif
        return renderable;

    }

    std::unique_ptr<procgen::TerrainRenderable> ChunkManager::buildRenderable(ChunkCoord center, uint64_t frameIndex)
    {
        using namespace procgen;

        ChunkCoord neighbors[25];
        get5x5Neighborhood(center, neighbors); // Guarantees 3x3 is [0-8]

        // Collect chunk records (all should exist and be populated by now)
        std::array<ChunkRecord*, 25> recs{};
        for (int i = 0; i < 25; ++i) {
            recs[i] = getOrCreateRecord(neighbors[i]);
        }

        auto renderable = std::make_unique<TerrainRenderable>();
        renderable->center = center;

        // Compute world bounds for core 3x3 and support 5x5
        const float cs = cfg_.chunkSize;
        const glm::vec2 coreMin = { (center.x - 1) * cs, (center.z - 1) * cs };
        const glm::vec2 coreMax = { (center.x + 2) * cs, (center.z + 2) * cs };
        const glm::vec2 suppMin = { (center.x - 2) * cs, (center.z - 2) * cs };
        const glm::vec2 suppMax = { (center.x + 3) * cs, (center.z + 3) * cs };

        // ----- Pass 1: Build global vertex remap -----
        // We need a global vertex index for every unique (chunk, localSiteIdx) pair.
        // Layout: [3x3 core verts | 3x3 halo verts | outer-16 all verts]
        //          ^-- "core" for alignment --^  ^-- "halo" for alignment --^

        // TODO: convert to scratch 
        struct ChunkVertexInfo {
            uint32_t globalBase = 0;
            uint32_t coreCount  = 0;
            uint32_t totalCount = 0;
        };
        std::array<ChunkVertexInfo, 25> chunkInfo{};

        // Count vertices
        uint32_t totalCoreVerts = 0;
        uint32_t totalHaloVerts = 0;

        // Inner 3x3: core verts go to the core section, non-core go to halo
        for (int i = 0; i < 9; ++i) {
            auto* ap = recs[i]->allPoints;
            if (!ap) { continue; }
            chunkInfo[i].coreCount  = static_cast<uint32_t>(ap->coreIdx.size());
            chunkInfo[i].totalCount = static_cast<uint32_t>(ap->pts.size());
            totalCoreVerts += chunkInfo[i].coreCount;
            totalHaloVerts += (chunkInfo[i].totalCount - chunkInfo[i].coreCount);
        }

        // Outer 16: all their verts are halo/support
        for (int i = 9; i < 25; ++i) {
            auto* ap = recs[i]->allPoints;
            if (!ap) { continue; }
            chunkInfo[i].coreCount  = 0; // No contribution to inner 3x3 core region
            chunkInfo[i].totalCount = static_cast<uint32_t>(ap->pts.size());
            totalHaloVerts += chunkInfo[i].totalCount;
        }

        const uint32_t totalVerts = totalCoreVerts + totalHaloVerts;

        // Assign global base offsets
        // Core region: pack 3x3 core verts contiguously
        uint32_t coreOffset = 0;
        for (int i = 0; i < 9; ++i) {
            chunkInfo[i].globalBase = coreOffset; // base for this chunk's core verts
            coreOffset += chunkInfo[i].coreCount;
        }

        // Halo region: pack 3x3 halo verts, then outer-16 verts
        uint32_t haloOffset = totalCoreVerts;
        // (We'll assign individual halo offsets per chunk during the packing pass)

        // ----- Pass 2: Pack positions and build per-chunk old2global remap -----
        renderable->packedPositions.resize(totalVerts);

        // Per-chunk remap: chunk-local site index -> global vertex index
        // We use a vector of vectors (stack allocated in terms of references)
        std::vector<std::vector<uint32_t>> old2global(25);

        for (int i = 0; i < 25; ++i) {
            auto* ap = recs[i]->allPoints;
            if (!ap) continue;

            const auto& pts = ap->pts;
            const auto& coreIdx = ap->coreIdx;
            const size_t nPts = pts.size();
            const auto& eHeights = (i < 9 && recs[i]->erosion) ? recs[i]->erosion->eHeights
                                                               : recs[i]->heightField->heights;

            // Create a mapping to translate local chunk points to a global index
            constexpr uint32_t UNMAPPED = std::numeric_limits<uint32_t>::max();
            old2global[i].resize(nPts, UNMAPPED);

            // Build core membership for this chunk
            std::vector<uint8_t> isCore(nPts, 0);
            if (i < 9) {
                for (uint32_t ci : coreIdx) {
                    // Flag generated core points of the 3x3 region
                    isCore[ci] = 1;
                }
            }

            // Region delineation
            if (i < 9) {
                // Inner 3x3: core verts get the core base, halo verts get halo base
                uint32_t localCoreOff = chunkInfo[i].globalBase;
                for (uint32_t ci : coreIdx) {
                    uint32_t gIdx = localCoreOff++;
                    old2global[i][ci] = gIdx;
                    renderable->packedPositions[gIdx] = glm::vec4(pts[ci].x, -eHeights[ci], pts[ci].y, 1.0f);
                }
                for (size_t si = 0; si < nPts; ++si) {
                    if (!isCore[si]) {
                        uint32_t gIdx = haloOffset++;
                        old2global[i][si] = gIdx;
                        renderable->packedPositions[gIdx] = glm::vec4(pts[si].x, -eHeights[si], pts[si].y, 1.0f);
                    }
                }
            } else {
                // Outer 16: all verts go to halo section
                for (size_t si = 0; si < nPts; ++si) {
                    uint32_t gIdx = haloOffset++;
                    old2global[i][si] = gIdx;
                    renderable->packedPositions[gIdx] = glm::vec4(pts[si].x, -eHeights[si], pts[si].y, 1.0f);
                }
            }
        }

        // ----- Pass 3: Pack triangles [core | halo] with rebased indices -----
        // First pass: count core vs halo triangles
        uint32_t totalCoreTris = 0;
        uint32_t totalHaloTris = 0;

        for (int i = 0; i < 25; ++i) {
            auto* tri = recs[i]->triangulation;
            if (!tri) continue;

            auto* ap = recs[i]->allPoints;
            if (!ap) continue;

            const size_t nPts = ap->pts.size();
            std::vector<uint8_t> isCore(nPts, 0);
            if (i < 9) {
                for (uint32_t ci : ap->coreIdx) isCore[ci] = 1;
            }

            for (const auto& t : tri->tris) {
                if (i < 9 && isCore[t.A] && isCore[t.B] && isCore[t.C]) {
                    ++totalCoreTris;
                } else {
                    ++totalHaloTris;
                }
            }
        }

        const uint32_t totalTris = totalCoreTris + totalHaloTris;
        renderable->packedTriangles.resize(totalTris);

        uint32_t coreTriOffset = 0;
        uint32_t haloTriOffset = totalCoreTris;

        for (int i = 0; i < 25; ++i) {
            auto* tri = recs[i]->triangulation;
            if (!tri) { continue; }

            auto* ap = recs[i]->allPoints;
            if (!ap) { continue; }

            const size_t nPts = ap->pts.size();
            std::vector<uint8_t> isCore(nPts, 0);
            if (i < 9) {
                for (uint32_t ci : ap->coreIdx) isCore[ci] = 1;
            }

            for (const auto& t : tri->tris) {
                uint32_t gA = old2global[i][t.A];
                uint32_t gB = old2global[i][t.B];
                uint32_t gC = old2global[i][t.C];

                if (i < 9 && isCore[t.A] && isCore[t.B] && isCore[t.C]) {
                    renderable->packedTriangles[coreTriOffset++] = packTriIndices(gA, gB, gC);
                } else {
                    renderable->packedTriangles[haloTriOffset++] = packTriIndices(gA, gB, gC);
                }
            }
        }

        // ----- Pass 4: Build adjacency [core | halo] -----
        renderable->packedAdjacency.resize(totalVerts);
        std::memset(renderable->packedAdjacency.data(), 0, totalVerts * sizeof(VertexAdjacency));

        // We need to iterate triangles again with the global triangle index
        // to assign globally-rebased triangle IDs to each vertex's adjacency.
        coreTriOffset = 0;
        haloTriOffset = totalCoreTris;

        for (int i = 0; i < 25; ++i) {
            auto* tri = recs[i]->triangulation;
            if (!tri) continue;

            auto* ap = recs[i]->allPoints;
            if (!ap) continue;

            const size_t nPts = ap->pts.size();
            std::vector<uint8_t> isCore(nPts, 0);
            if (i < 9) {
                for (uint32_t ci : ap->coreIdx) isCore[ci] = 1;
            }

            for (const auto& t : tri->tris) {
                uint32_t gA = old2global[i][t.A];
                uint32_t gB = old2global[i][t.B];
                uint32_t gC = old2global[i][t.C];

                uint32_t globalTriIdx;
                if (i < 9 && isCore[t.A] && isCore[t.B] && isCore[t.C]) {
                    globalTriIdx = coreTriOffset++;
                } else {
                    globalTriIdx = haloTriOffset++;
                }

                const uint32_t verts[3] = { gA, gB, gC };
                for (int k = 0; k < 3; ++k) {
                    auto& adj = renderable->packedAdjacency[verts[k]];
                    if (adj.count < MAX_ADJACENT_TRIS) {
                        adj.triangleIndices[adj.count++] = globalTriIdx;
                    }
                }
            }
        }

        // ----- Pass 5: Build VBO and IBO (3x3 core only) -----
        // VBO = the core positions (already packed at indices [0..totalCoreVerts))
        renderable->vbo.resize(totalCoreVerts);
        for (uint32_t v = 0; v < totalCoreVerts; ++v) {
            renderable->vbo[v] = renderable->packedPositions[v];
            glm::vec3 vert = renderable->packedPositions[v];
            //if (v > 4000) {
            //    std::printf("V1 vbo[%d] = {%f, %f, %f}\n", v, vert.x, vert.y, vert.z);
            //}
        }

        // IBO = core triangles with indices remapped into VBO space
        // Core triangles already reference global indices [0..totalCoreVerts),
        // and VBO is a direct copy of that range, so VBO-local index = global index.
        renderable->ibo.reserve(totalCoreTris * 3);
        for (uint32_t ti = 0; ti < totalCoreTris; ++ti) {
            const glm::vec3& packed = renderable->packedTriangles[ti];
            uint32_t a, b, c;
            std::memcpy(&a, &packed.x, sizeof(uint32_t));
            std::memcpy(&b, &packed.y, sizeof(uint32_t));
            std::memcpy(&c, &packed.z, sizeof(uint32_t));
            renderable->ibo.push_back(a);
            renderable->ibo.push_back(b);
            renderable->ibo.push_back(c);
        }

        // ----- Fill alignment UBO -----
        auto& align = renderable->alignment;
        align.baseCorePosition  = 0;
        align.countCorePosition = totalCoreVerts;
        align.countHaloPosition = totalHaloVerts;

        align.baseCoreTriangle  = 0;
        align.countCoreTriangle = totalCoreTris;
        align.countHaloTriangle = totalHaloTris;

        align.baseCoreAdjacency  = 0;
        align.countCoreAdjacency = totalCoreVerts;
        align.countHaloAdjacency = totalHaloVerts;

        align.coreMinXZ    = coreMin;
        align.coreMaxXZ    = coreMax;
        align.supportMinXZ = suppMin;
        align.supportMaxXZ = suppMax;

        return renderable;
    }

    bool ChunkManager::tryTakeRenderable(
        ChunkCoord center,
        uint64_t requestId,
        std::unique_ptr<procgen::TerrainRenderable>& out)
    {
        ChunkRecord* rec = getOrCreateRecord(center);
        if (!rec) { return false; }

        std::scoped_lock lock(rec->renderableMutex);

        if (rec->renderableState != RenderableBuildState::Ready) {
            return false;
        }

        if (rec->completedRenderableId != requestId) {
            return false; // stale completion or newer request replaced it
        }

        if (!rec->renderableResult) {
            return false;
        }

        out = std::move(rec->renderableResult);

        // Keep or clear state depending on caching policy.
        rec->renderableState = RenderableBuildState::None;
        rec->renderablePublished = false;

        return true;
    }


    /* Lifetime saftey features & eviction policy (below) */

    // Pin based on chunk coord - this can end up creating a chunk record
    ChunkRecord* ChunkManager::pin(ChunkCoord c, uint64_t frameIndex) {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->pinCount.fetch_add(1, std::memory_order_relaxed);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);
        return rec;
    }
    // Pin via pointer - for when we already have the record
    void ChunkManager::pin(ChunkRecord* rec, uint64_t frameIndex) {
        rec->pinCount.fetch_add(1, std::memory_order_relaxed);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);
    }
    // Pin without perturbing frame count (touch)
    void ChunkManager::pin(ChunkRecord* rec) {
        rec->pinCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Unpin from pointer - Note: Don't ever do unpin(ChunkCoord) because passing a coord implies we'd call getOrCreateRecord (at the moment)
    void ChunkManager::unpin(ChunkRecord* rec) {
        rec->pinCount.fetch_sub(1, std::memory_order_relaxed);
    }

    // See https://chatgpt.com/share/698e7ab7-08a4-800a-86e1-291e1041e496 for more info
    void ChunkManager::evictUnpinnedOlderThan(uint64_t frameIndex, uint64_t ageFrames) {
        for (size_t s = 0; s < STRIPES; ++s) {
            std::vector<std::unique_ptr<ChunkRecord>> toFree;
            toFree.reserve(32); // heuristic

            {
                auto& bucket = records_[s];
                std::lock_guard<std::mutex> lock(bucket.mut);

                for (auto it = bucket.map.begin(); it != bucket.map.end(); ) {
                    ChunkRecord* rec = it->second.get();

                    const int32_t pins = rec->pinCount.load(std::memory_order_relaxed);
                    const uint64_t last = rec->lastTouchedFrame.load(std::memory_order_relaxed);
                    const uint64_t age = (frameIndex >= last) ? (frameIndex - last) : 0;

                    if (pins == 0 && age > ageFrames) {
                        clearAllPointsReady(rec->coord);
                        // Move the chunk record out of its bucket
                        // into a local that won't survive out of scope.
                        // Important: This frees all pmr resources automatically,
                        // to go where all ways must lead us, eventually.
                        toFree.emplace_back(std::move(it->second));
                        it = bucket.map.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            } // unlock stripe before freeing memory
        }
    }

}