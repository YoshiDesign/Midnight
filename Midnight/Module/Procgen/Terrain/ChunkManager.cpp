#include <chrono>
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
#include "Utils/Logger.h"

#ifdef M_DEBUG
#include <filesystem>
#include <string>
#include <cstdint>
#include <format>
#include "Runtime/Debug.h"
#endif
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

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
     * without the need to add more costly synchronization to keep the scheduler happy.
     * Note: this is a working hypothesis due to a deadlocking scenario :)
     * This readiness indication can belong to any stage. I'm just being optimistic
     * by giving it to the (early) requestAllPoints stage.
     * 
     * If we tighten up streaming policy we could maybe factor out the mutex
     * but that's beyond micro-optimizing at the moment.
     */
    bool ChunkManager::isSpatialGridReady(const ChunkCoord coord) const {
        std::lock_guard<std::mutex> lock(allSpatialGridReadyMut_);
        return allSpatialGridReady_.find(coord) != allSpatialGridReady_.end();
    }

    bool ChunkManager::isRegionSpatialGridReady(ChunkCoord center) const {
        ChunkCoord region[25];
        get5x5Neighborhood(center, region);

        std::lock_guard<std::mutex> lock(allSpatialGridReadyMut_);
        for (int i = 0; i < 25; ++i) {
            if (allSpatialGridReady_.find(region[i]) == allSpatialGridReady_.end())
                return false;
        }
        return true;
    }

    void ChunkManager::markSpatialGridReady(ChunkCoord coord) {
        std::lock_guard<std::mutex> lock(allSpatialGridReadyMut_);
        allSpatialGridReady_.insert(coord);
        // Logger::log(1, "READY {%d, %d}\n", coord.x, coord.z);
    }

    void ChunkManager::clearSpatialGridReady(ChunkCoord coord) {
        std::lock_guard<std::mutex> lock(allSpatialGridReadyMut_);
        allSpatialGridReady_.erase(coord);
    }

    bool ChunkManager::isRegionAllStagesComplete(ChunkCoord center) const {
        ChunkCoord core[9];
        get3x3Neighborhood(center, core);

        std::lock_guard<std::mutex> lock(allStagesCompleteMut_);
        for (int i = 0; i < 9; ++i) {
            if (allStagesComplete_.find(core[i]) == allStagesComplete_.end())
                return false;
        }
        return true;
    }

    void ChunkManager::markAllStagesComplete(ChunkCoord coord) {
        std::lock_guard<std::mutex> lock(allStagesCompleteMut_);
        allStagesComplete_.insert(coord);
    }

    void ChunkManager::clearAllStagesComplete(ChunkCoord coord) {
        std::lock_guard<std::mutex> lock(allStagesCompleteMut_);
        allStagesComplete_.erase(coord);
    }

    //
    ChunkRecord* ChunkManager::getOrCreateRecord(ChunkCoord coord)
    {
        const size_t hash = ChunkCoordHash{}(coord); // turns (x,z) into a size_t
        
#ifdef MIDNIGHT_WYHASH
        const size_t stripeIdx = stripeIndexwh(coord);
#else
        const size_t stripeIdx = hash & (STRIPES - 1);
#endif
        auto& bucket = records_[stripeIdx];

        // Nifty RAII lock_guard - 
        // Note: This is a contentious thing to do when generating in parallel
        // TODO: Consider a higher level invariant to prevent this necessity at all
        // assuming this lock is only here to prevent the usual concurrency pitfalls.
        // Either way this class needs an audit in safety overkill
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

    // -----------------------------------------------------------------------
    // Stage request methods
    //
    // Each request* method:
    //   1. Gets/creates the record and touches lastTouchedFrame
    //   2. CAS NotStarted->Queued ensures the stage is initiated exactly once
    //   3. Enqueues run*Stage on the thread pool
    //
    // Each run*Stage method:
    //   1. Ensures prerequisites are requested (idempotent via their CAS guard)
    //   2. Checks prerequisite readiness via atomic load (acquire)
    //   3. If not ready: re-enqueues itself (guarded by retry flag to prevent flooding)
    //   4. If ready: pins records, builds, stores Ready (release)
    // -----------------------------------------------------------------------

    void ChunkManager::requestMesh(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        auto expected = StageState::NotStarted;
        if (rec->meshState.compare_exchange_strong(expected, StageState::Queued)) {
            tasks_.enqueue([this, coord = c, frameIndex]() {
                ChunkRecord* r = getOrCreateRecord(coord);
                runMeshStage(*r, frameIndex);
            });
        }
    }

    void ChunkManager::runMeshStage(ChunkRecord& rec, uint64_t frameIndex) {
        RecordPin hold(*this, &rec, frameIndex);
        requestErosion(rec.coord, frameIndex);

        if (rec.erosionState.load(std::memory_order_acquire) != StageState::Ready) {
            bool expected = false;
            if (rec.meshRetryQueued.compare_exchange_strong(expected, true)) {
                tasks_.enqueue([this, coord = rec.coord, frameIndex]() {
                    ChunkRecord* again = getOrCreateRecord(coord);
                    again->meshRetryQueued.store(false, std::memory_order_relaxed);
                    runMeshStage(*again, frameIndex);
                });
            }
            return;
        }

        // buildMesh is currently disabled; mark complete
        rec.meshState.store(StageState::Ready, std::memory_order_release);
    }

    // Points -- no upstream dependency
    void ChunkManager::requestPoints(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        auto expected = StageState::NotStarted;
        if (rec->pointsState.compare_exchange_strong(expected, StageState::Queued)) {
            tasks_.enqueue([this, coord = c]() {
                ChunkRecord* r = getOrCreateRecord(coord);
                RecordPin taskHold(*this, r);
                buildPoints(*r);
                r->pointsState.store(StageState::Ready, std::memory_order_release);
            });
        }
    }

    // AllPoints -- depends on 9 neighbor Points
    void ChunkManager::requestAllPoints(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        auto expected = StageState::NotStarted;
        if (rec->allPointsState.compare_exchange_strong(expected, StageState::Queued)) {
            tasks_.enqueue([this, coord = c, frameIndex]() {
                ChunkRecord* r = getOrCreateRecord(coord);
                runAllPointsStage(*r, frameIndex);
            });
        }
    }

    void ChunkManager::runAllPointsStage(ChunkRecord& rec, uint64_t frameIndex) {
        std::array<ChunkCoord, 9> neighbors;
        get3x3Neighborhood(rec.coord, neighbors.data());

        // Pin self + 9 neighbors before readiness checks
        RecordPin selfHold(*this, &rec, frameIndex);
        ChunkRecord* nrecs[9];
        std::array<RecordPin, 9> neighborHolds;
        for (int i = 0; i < 9; ++i) {
            nrecs[i] = getOrCreateRecord(neighbors[i]);
            neighborHolds[i] = RecordPin(*this, nrecs[i], frameIndex);
        }

        for (int i = 0; i < 9; ++i) {
            requestPoints(neighbors[i], frameIndex);
        }

        for (int i = 0; i < 9; ++i) {
            if (nrecs[i]->pointsState.load(std::memory_order_acquire) != StageState::Ready) {
                bool expected = false;
                if (rec.allPointsRetryQueued.compare_exchange_strong(expected, true)) {
                    tasks_.enqueue([this, coord = rec.coord, frameIndex]() {
                        ChunkRecord* again = getOrCreateRecord(coord);
                        again->allPointsRetryQueued.store(false, std::memory_order_relaxed);
                        runAllPointsStage(*again, frameIndex);
                    });
                }
                return;
            }
        }

        buildAllPoints(rec);
        rec.allPointsState.store(StageState::Ready, std::memory_order_release);
    }

    // Heights
    void ChunkManager::requestHeights(ChunkCoord c, uint64_t frameIndex) {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        auto expected = StageState::NotStarted;
        if (rec->heightsState.compare_exchange_strong(expected, StageState::Queued)) {
            tasks_.enqueue([this, coord = c, frameIndex]() {
                ChunkRecord* r = getOrCreateRecord(coord);
                runHeightsStage(*r, frameIndex);
            });
        }
    }

    void ChunkManager::runHeightsStage(ChunkRecord& rec, uint64_t frameIndex) {
        RecordPin hold(*this, &rec, frameIndex);
        requestAllPoints(rec.coord, frameIndex);

        if (rec.allPointsState.load(std::memory_order_acquire) != StageState::Ready) {
            bool expected = false;
            if (rec.heightsRetryQueued.compare_exchange_strong(expected, true)) {
                tasks_.enqueue([this, coord = rec.coord, frameIndex]() {
                    ChunkRecord* again = getOrCreateRecord(coord);
                    again->heightsRetryQueued.store(false, std::memory_order_relaxed);
                    runHeightsStage(*again, frameIndex);
                });
            }
            return;
        }

        buildHeights(rec);
        rec.heightsState.store(StageState::Ready, std::memory_order_release);
    }

    // Triangulation
    void ChunkManager::requestTriangulation(ChunkCoord c, uint64_t frameIndex) {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        auto expected = StageState::NotStarted;
        if (rec->triangState.compare_exchange_strong(expected, StageState::Queued)) {
            tasks_.enqueue([this, coord = c, frameIndex]() {
                ChunkRecord* r = getOrCreateRecord(coord);
                runTriangulationStage(*r, frameIndex);
            });
        }
    }

    void ChunkManager::runTriangulationStage(ChunkRecord& rec, uint64_t frameIndex) {
        RecordPin hold(*this, &rec, frameIndex);
        requestHeights(rec.coord, frameIndex);

        if (rec.heightsState.load(std::memory_order_acquire) != StageState::Ready) {
            bool expected = false;
            if (rec.triangRetryQueued.compare_exchange_strong(expected, true)) {
                tasks_.enqueue([this, coord = rec.coord, frameIndex]() {
                    ChunkRecord* again = getOrCreateRecord(coord);
                    again->triangRetryQueued.store(false, std::memory_order_relaxed);
                    runTriangulationStage(*again, frameIndex);
                });
            }
            return;
        }

        buildTriangulation(rec);
        rec.triangState.store(StageState::Ready, std::memory_order_release);
    }

    // SpatialGrid
    void ChunkManager::requestSpatialGrid(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        auto expected = StageState::NotStarted;
        if (rec->spatialState.compare_exchange_strong(expected, StageState::Queued)) {
            tasks_.enqueue([this, coord = c, frameIndex]() {
                ChunkRecord* r = getOrCreateRecord(coord);
                runSpatialGridStage(*r, frameIndex);
            });
        }
    }

    void ChunkManager::runSpatialGridStage(ChunkRecord& rec, uint64_t frameIndex) {
        RecordPin hold(*this, &rec, frameIndex);
        requestTriangulation(rec.coord, frameIndex);

        if (rec.triangState.load(std::memory_order_acquire) != StageState::Ready) {
            bool expected = false;
            if (rec.spatialRetryQueued.compare_exchange_strong(expected, true)) {
                tasks_.enqueue([this, coord = rec.coord, frameIndex]() {
                    ChunkRecord* again = getOrCreateRecord(coord);
                    again->spatialRetryQueued.store(false, std::memory_order_relaxed);
                    runSpatialGridStage(*again, frameIndex);
                });
            }
            return;
        }

        buildSpatialGrid(rec);
        markSpatialGridReady(rec.coord);
        rec.spatialState.store(StageState::Ready, std::memory_order_release);
    }

    // Erosion
    void ChunkManager::requestErosion(ChunkCoord c, uint64_t frameIndex) {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        auto expected = StageState::NotStarted;
        if (rec->erosionState.compare_exchange_strong(expected, StageState::Queued)) {
            // Snapshot settings into the build context (persists across retries)
            rec->erosionCtx.settings = erosionMgr_ ? erosionMgr_->getActiveSettings() : ErosionSettings{};

            tasks_.enqueue([this, coord = c, frameIndex]() {
                ChunkRecord* r = getOrCreateRecord(coord);
                runErosionStage(*r, frameIndex);
            });
        }
    }

    void ChunkManager::runErosionStage(ChunkRecord& rec, uint64_t frameIndex) {
        RecordPin hold(*this, &rec, frameIndex);
        requestSpatialGrid(rec.coord, frameIndex);

        if (rec.spatialState.load(std::memory_order_acquire) != StageState::Ready) {
            bool expected = false;
            if (rec.erosionRetryQueued.compare_exchange_strong(expected, true)) {
                tasks_.enqueue([this, coord = rec.coord, frameIndex]() {
                    ChunkRecord* again = getOrCreateRecord(coord);
                    again->erosionRetryQueued.store(false, std::memory_order_relaxed);
                    runErosionStage(*again, frameIndex);
                });
            }
            return;
        }

        bool done = advanceErosion(rec);

        if (!done) {
            bool expected = false;
            if (rec.erosionRetryQueued.compare_exchange_strong(expected, true)) {
                tasks_.enqueue([this, coord = rec.coord, frameIndex]() {
                    ChunkRecord* again = getOrCreateRecord(coord);
                    again->erosionRetryQueued.store(false, std::memory_order_relaxed);
                    runErosionStage(*again, frameIndex);
                });
            }
            return;
        }

        markAllStagesComplete(rec.coord);
        rec.erosionState.store(StageState::Ready, std::memory_order_release);
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
        
        // 2) Allocate AllPoints struct in chunk final (persists across stages)
        if (!rec.allPoints) {
            auto alloc = std::pmr::polymorphic_allocator<AllPoints>(rec.final.mr());
            rec.allPoints = alloc.allocate(1);
            std::construct_at(rec.allPoints, rec.final.mr());
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

        // If we ever get called redundantly (shouldn't happen with CAS guard),
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
        // Return stable pointer into the optional
        return &(*rec.spatial);

    }

    // -----------------------------------------------------------------------
    // advanceErosion -- retry-driven state machine
    // Returns true when the erosion build is complete. Returns false when
    // batch futures are still in flight (caller should defer and re-enqueue).
    // -----------------------------------------------------------------------
    bool ChunkManager::advanceErosion(ChunkRecord& rec)
    {
        using Phase = procgen::ErosionBuildContext::Phase;
        auto& ctx = rec.erosionCtx;
        auto& s   = ctx.settings;

        // ------ Phase: NotStarted (init) ------
        if (ctx.phase == Phase::NotStarted) {
            rec.scratch.reset();
            auto* scratchMr = rec.scratch.mr();

            ctx.ws = std::make_unique<procgen::ErosionWorkingSet>(scratchMr);
            auto& ws = *ctx.ws;

            ctx.hydroSeed   = wyhash64(rec.chunkSeed, SeedTag::Hydraulic);
            ctx.thermalSeed = wyhash64(rec.chunkSeed, SeedTag::Thermal);
            ctx.ridgeSeed   = wyhash64(rec.chunkSeed, SeedTag::Ridge);

            auto const& heights = rec.heightField->heights;
            const size_t N = heights.size();

            ws.workHeights.resize(N);
            ws.delta.resize(N);
            ws.hardness.resize(N);

            std::copy(heights.begin(), heights.end(), ws.workHeights.begin());
            std::fill(ws.delta.begin(), ws.delta.end(), 0.0f);

            if (s.hardnessMapEnabled) {
                const uint64_t hardnessSeed = wyhash64(rec.chunkSeed, SeedTag::Hardness);
                procgen::ComputeHardnessMap(
                    ws.hardness,
                    rec.allPoints->pts,
                    ws.workHeights,
                    s.hardness,
                    hardnessSeed
                );
            }

            ctx.phase = Phase::HydraulicSubmitted;
            // fall through to submit hydraulic
        }

        auto& ws = *ctx.ws;

        // ------ Phase: HydraulicSubmitted ------
        if (ctx.phase == Phase::HydraulicSubmitted) {
            if (s.hydraulicErosionEnabled) {
                if (ctx.valueFutures.empty()) {
                    ctx.valueFutures = procgen::SubmitHydraulicBatches(
                        ws, *rec.allPoints, *rec.triangulation, rec.spatial.value(),
                        s.hydraulic, ctx.hydroSeed, tasks_
                    );
                    if (!ctx.valueFutures.empty())
                        return false;
                }

                if (!ctx.allValueFuturesReady())
                    return false;

                procgen::ReduceHydraulicResults(ws, ctx.valueFutures);
                ctx.clearFutures();

                ApplyDelta(ws.workHeights, ws.delta);
                std::fill(ws.delta.begin(), ws.delta.end(), 0.0f);
            }

            ctx.phase = Phase::ThermalSubmitted;
            ctx.currentIteration = 0;
            // fall through
        }

        // ------ Phase: ThermalSubmitted ------
        if (ctx.phase == Phase::ThermalSubmitted) {
            if (s.thermalErosionEnabled && s.thermal.Iterations > 0) {
                // One-time init on first entry
                if (ctx.currentIteration == 0 && ctx.thermalWorkers == 0) {
                    procgen::InitThermalErosion(ws, s.thermal, ctx.thermalWorkers, ctx.thermalBatchSz);
                }

                while (ctx.currentIteration < s.thermal.Iterations) {
                    if (ctx.valueFutures.empty()) {
                        ctx.valueFutures = procgen::SubmitThermalIterationBatches(
                            ws, *rec.allPoints, *rec.triangulation, s.thermal,
                            ctx.thermalWorkers, ctx.thermalBatchSz, tasks_
                        );
                        if (!ctx.valueFutures.empty())
                            return false;
                    }

                    if (!ctx.allValueFuturesReady())
                        return false;

                    procgen::ReduceThermalIteration(ws, ctx.valueFutures);
                    ctx.clearFutures();

                    ctx.currentIteration++;
                }

                procgen::FinalizeThermalDelta(ws);
                ApplyDelta(ws.workHeights, ws.delta);
                std::fill(ws.delta.begin(), ws.delta.end(), 0.0f);
            }

            ctx.phase = Phase::RidgeSubmitted;
            ctx.currentIteration = 0;
            ctx.ridgeSubPhase = 0;
            // fall through
        }

        // ------ Phase: RidgeSubmitted ------
        if (ctx.phase == Phase::RidgeSubmitted) {
            if (s.ridgeEnhancementEnabled && s.ridges.Iterations > 0) {
                // One-time init on first entry
                if (ctx.currentIteration == 0 && ctx.ridgeMaxWorkers == 0) {
                    procgen::InitRidgeEnhancement(ws, s.ridges,
                        ctx.ridgeMinH, ctx.ridgeMaxH, ctx.ridgeMaxWorkers);
                }

                while (ctx.currentIteration < (uint32_t)s.ridges.Iterations) {
                    // Sub-phase 0: compute ridgeness
                    if (ctx.ridgeSubPhase == 0) {
                        if (ctx.voidFutures.empty()) {
                            ctx.voidFutures = procgen::SubmitRidgenessCompute(
                                ws, *rec.triangulation, ctx.ridgeMaxWorkers, tasks_);
                            if (!ctx.voidFutures.empty())
                                return false;
                        }
                        if (!ctx.allVoidFuturesReady())
                            return false;
                        ctx.clearFutures();
                        ctx.ridgeSubPhase = 1;
                    }

                    // Sub-phase 1: copy heights
                    if (ctx.ridgeSubPhase == 1) {
                        if (ctx.voidFutures.empty()) {
                            ctx.voidFutures = procgen::SubmitRidgeCopy(
                                ws, ctx.ridgeMaxWorkers, tasks_);
                            if (!ctx.voidFutures.empty())
                                return false;
                        }
                        if (!ctx.allVoidFuturesReady())
                            return false;
                        ctx.clearFutures();
                        ctx.ridgeSubPhase = 2;
                    }

                    // Sub-phase 2: apply enhancement
                    if (ctx.ridgeSubPhase == 2) {
                        if (ctx.voidFutures.empty()) {
                            ctx.voidFutures = procgen::SubmitRidgeApply(
                                ws, *rec.allPoints, s.ridges, ctx.ridgeSeed,
                                ctx.ridgeMinH, ctx.ridgeMaxH, ctx.ridgeMaxWorkers, tasks_);
                            if (!ctx.voidFutures.empty())
                                return false;
                        }
                        if (!ctx.allVoidFuturesReady())
                            return false;
                        ctx.clearFutures();
                    }

                    procgen::SwapRidgeIteration(ws);
                    ctx.currentIteration++;
                    ctx.ridgeSubPhase = 0;
                }
            }

            ctx.phase = Phase::Finalize;
            // fall through
        }

        // ------ Phase: Finalize ------
        if (ctx.phase == Phase::Finalize) {
            if (!rec.erosion) {
                auto alloc = std::pmr::polymorphic_allocator<ErosionField>(rec.final.mr());
                rec.erosion = alloc.allocate(1);
                std::construct_at(rec.erosion, rec.final.mr());
            }

            rec.erosion->eHeights.clear();
            rec.erosion->eHeights.reserve(ws.workHeights.size());
            rec.erosion->eHeights.insert(rec.erosion->eHeights.end(),
                ws.workHeights.begin(), ws.workHeights.end());

#ifdef M_DEBUG
            // std::printf("[Chunk Completed] {%d, %d}\n", rec.coord.x, rec.coord.z);
#endif

            ctx.ws.reset();
            ctx.phase = Phase::Done;
        }

        return true;
    }

    //FinalMeshCPU const* ChunkManager::buildMesh(ChunkRecord& rec)
    //{
    //    if( rec.points == nullptr 
    //     || rec.allPoints == nullptr
    //     || rec.heightField == nullptr
    //     || rec.triangulation == nullptr
    //     || rec.erosion == nullptr
    //    ) { return nullptr; }

    //    const auto& pts       = rec.allPoints->pts;
    //    const auto& coreIdx   = rec.allPoints->coreIdx;
    //    const auto& triangles = rec.triangulation->tris;
    //    const auto& eHeights  = rec.erosion->eHeights;
    //    const size_t totalPts = pts.size();
    //    const size_t numCoreVerts = coreIdx.size();

    //    // Allocate FinalMeshCPU in durable (final) arena
    //    if (!rec.finalMesh) {
    //        auto alloc = std::pmr::polymorphic_allocator<FinalMeshCPU>(rec.final.mr());
    //        rec.finalMesh = alloc.allocate(1);
    //        std::construct_at(rec.finalMesh, rec.final.mr());
    //    }
    //    FinalMeshCPU& mesh = *rec.finalMesh;

    //    // -- Core membership mask (flat, no hashing) --
    //    std::pmr::vector<uint8_t> isCore(totalPts, uint8_t(0), tlsScratchArena().mr());
    //    for (uint32_t ci : coreIdx) {
    //        // for 4-5k points this is trivially expensive
    //        // but there should be a faster way to fill a vector with 1's
    //        isCore[ci] = 1;
    //    }

    //    // -- old2new remap: global site index -> core-only VBO index --
    //    constexpr uint32_t UNMAPPED = std::numeric_limits<uint32_t>::max();
    //    std::pmr::vector<uint32_t> old2new(
    //        totalPts, 
    //        UNMAPPED,
    //        tlsScratchArena().mr()
    //    );

    //    // 1a. VBO positions (core only)
    //    mesh.vbo_positions.clear();
    //    mesh.vbo_positions.reserve(numCoreVerts);
    //    for (uint32_t ci : coreIdx) {
    //        old2new[ci] = static_cast<uint32_t>(mesh.vbo_positions.size());
    //        mesh.vbo_positions.push_back(glm::vec3(pts[ci].x, -eHeights[ci], pts[ci].y));
    //    }

    //    // 1b/1c. IBO indices + tris (core triangles only)
    //    mesh.ibo_indices.clear();
    //    mesh.tris.clear();
    //    mesh.ibo_indices.reserve(triangles.size() * 3);
    //    mesh.tris.reserve(triangles.size());

    //    for (const auto& tri : triangles) {
    //        if (!(isCore[tri.A] && isCore[tri.B] && isCore[tri.C])) { continue; }

    //        mesh.ibo_indices.push_back(old2new[tri.A]);
    //        mesh.ibo_indices.push_back(old2new[tri.B]);
    //        mesh.ibo_indices.push_back(old2new[tri.C]);

    //        mesh.tris.push_back(packTriIndices(old2new[tri.A], old2new[tri.B], old2new[tri.C]));
    //    }

    //    // 1d. Packed positions: [core..., halo...]
    //    mesh.packed_positions.clear();
    //    mesh.packed_positions.reserve(totalPts);
    //    for (uint32_t ci : coreIdx) {
    //        mesh.packed_positions.push_back(glm::vec3(pts[ci].x, -eHeights[ci], pts[ci].y));
    //    }
    //    for (size_t i = 0; i < totalPts; ++i) {
    //        if (!isCore[i]) {
    //            mesh.packed_positions.push_back(glm::vec3(pts[i].x, -eHeights[i], pts[i].y));
    //        }
    //    }

    //    // 1e. Adjacency: one entry per vertex (core + halo), triangle indices are chunk-local
    //    mesh.adjacency.clear();
    //    mesh.adjacency.resize(totalPts);
    //    std::memset(mesh.adjacency.data(), 0, totalPts * sizeof(procgen::VertexAdjacency));

    //    for (uint32_t ti = 0; ti < static_cast<uint32_t>(triangles.size()); ++ti) {
    //        const SiteIndex verts[3] = { triangles[ti].A, triangles[ti].B, triangles[ti].C };
    //        for (int k = 0; k < 3; ++k) {
    //            auto& adj = mesh.adjacency[verts[k]];
    //            if (adj.count < procgen::MAX_ADJACENT_TRIS) {
    //                adj.triangleIndices[adj.count++] = ti;
    //            }
    //        }
    //    }

    //    return rec.finalMesh;
    //}

    /* Async */
    uint64_t ChunkManager::requestRenderableAsync(ChunkCoord center, uint64_t frameIndex,
        procgen::TerrainRenderable* target, uint32_t slotIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(center);

        uint64_t requestId = 0;
        bool shouldSubmit = false;

        {
            std::scoped_lock lock(rec->renderableMutex);

            if (rec->renderableState == RenderableBuildState::Queued ||
                rec->renderableState == RenderableBuildState::Building ||
                rec->renderableState == RenderableBuildState::Ready)
            {
                return rec->requestedRenderableId;
            }

            requestId = ++rec->requestedRenderableId;
            rec->renderableState = RenderableBuildState::Queued;
            rec->renderablePublished = false;
            rec->renderableTarget = target;
            rec->slotIndex = slotIndex;
            shouldSubmit = true;
        }

        if (shouldSubmit) {
            tasks_.enqueue([this, center, frameIndex, requestId]() {
                ChunkRecord* workerRec = getOrCreateRecord(center);
                RecordPin hold(*this, workerRec, frameIndex);

                {
                    std::scoped_lock lock(workerRec->renderableMutex);
                    if (workerRec->requestedRenderableId != requestId)
                        return;
                    workerRec->renderableState = RenderableBuildState::Building;
                }

                runGenerate(center, frameIndex, requestId);
            });
        }
        return requestId;
    }

    // ---------------------------------------------------------------------------
    // Renderable assembly: non-blocking
    //
    // Requests all 25 sub-stages (9 mesh + 16 spatial), checks readiness,
    // re-enqueues if not all deps are satisfied. When ready, pins everything
    // and builds the final renderable.
    // ---------------------------------------------------------------------------

    void ChunkManager::runGenerate(ChunkCoord center, uint64_t frameIndex, uint64_t requestId)
    {
        ChunkRecord* centerRec = getOrCreateRecord(center);

        // TODO : Validate the real necessity here
        {
            // Lots of contention here: we call runGenerate 25x per chunk (for a clean chunk w/ no overlap)
            //std::printf("[ChunkManager] Locking: centerRec->renderableMutex\n");
            std::scoped_lock lock(centerRec->renderableMutex);
            if (centerRec->requestedRenderableId != requestId)
                return;
        }

        ChunkCoord neighbors[25];
        get5x5Neighborhood(center, neighbors);

        /*
        * TODO - The fact that ChunkRecord* nrecs[25]; is initialized
        * locally irks me. We use std::move on its 4th index (center chunk)
        * in the buildRenderableV2 method to return the packed renderable, 
        * so the pattern is convenient, but is there a better way to do this?
        * 
        * Maybe the ChunkManager could organize chunks differently.
        * Instead of our StripeBucket, what if chunks existed in localized
        * 5x5 groups, since they're uploaded that way.
        * 
        * Note: this is the 2nd time in this call-stack that we've used
        * getOrCreateRecord to retrieve the center chunk.
        */

        // Pin all 25 records BEFORE readiness checks to prevent
        // evictRecord from destroying them while we hold raw pointers.
        ChunkRecord* nrecs[25];
        std::array<RecordPin, 25> pins;
        for (int i = 0; i < 25; ++i) {
            nrecs[i] = getOrCreateRecord(neighbors[i]);
            pins[i] = RecordPin(*this, nrecs[i], frameIndex);
        }

        // Kick off all sub-stages (idempotent via their CAS guards)
        for (int i = 0; i < 9; ++i) {
            requestMesh(neighbors[i], frameIndex);
        }
        for (int i = 9; i < 25; ++i) {
            requestSpatialGrid(neighbors[i], frameIndex);
        }

        // Check readiness of inner 9 (mesh) and outer 16 (spatial)
        for (int i = 0; i < 9; ++i) {
            if (nrecs[i]->meshState.load(std::memory_order_acquire) != StageState::Ready) {
                bool expected = false;
                if (centerRec->generateRetryQueued.compare_exchange_strong(expected, true)) {
                    tasks_.enqueue([this, center, frameIndex, requestId]() {
                        ChunkRecord* again = getOrCreateRecord(center);
                        again->generateRetryQueued.store(false, std::memory_order_relaxed);
                        runGenerate(center, frameIndex, requestId);
                    });
                }
                return;
            }
        }
        for (int i = 9; i < 25; ++i) {
            if (nrecs[i]->spatialState.load(std::memory_order_acquire) != StageState::Ready) {
                bool expected = false;
                if (centerRec->generateRetryQueued.compare_exchange_strong(expected, true)) {
                    tasks_.enqueue([this, center, frameIndex, requestId]() {
                        ChunkRecord* again = getOrCreateRecord(center);
                        again->generateRetryQueued.store(false, std::memory_order_relaxed);
                        runGenerate(center, frameIndex, requestId);
                    });
                }
                return;
            }
        }

        // procgen::traceStage((center, procgen::TerrainStage::Renderable, "begin");

        try {
            buildRenderablev2(center, frameIndex, nrecs);

            bool shouldPublish = false;
            uint32_t slotIdx = 0;
            {
                std::scoped_lock lock(centerRec->renderableMutex);
                if (centerRec->requestedRenderableId != requestId) {
                    return;
                }

                centerRec->completedRenderableId = requestId;
                centerRec->renderableState = RenderableBuildState::Ready;
                centerRec->renderablePublished = true;
                slotIdx = centerRec->slotIndex;
                shouldPublish = true;
            }

            if (shouldPublish) {
                if (admissionCtl_) {
                    admissionCtl_->release(center, admissionRadius_);
                }

                // push onto the ConcurrentQueue
                completedRenderables_.push(procgen::CompletionNotice{
                    .slotIndex = slotIdx,
                    .requestId = requestId,
                    .success = true
                });
            }
        }
        catch (...) {
            uint32_t slotIdx = 0;
            {
                std::scoped_lock lock(centerRec->renderableMutex);
                if (centerRec->requestedRenderableId == requestId) {
                    centerRec->renderableState = RenderableBuildState::Failed;
                }
                slotIdx = centerRec->slotIndex;
            }

            if (admissionCtl_) {
                admissionCtl_->release(center, admissionRadius_);
            }

            completedRenderables_.push(procgen::CompletionNotice{
                .slotIndex = slotIdx,
                .requestId = requestId,
                .success = false
            });
        }
    }

    void ChunkManager::generate(ChunkCoord center, uint64_t frameIndex, uint64_t requestId)
    {
        assert(false && "generate() is deprecated; use runGenerate() instead.");
    }

    void ChunkManager::buildRenderablev2(ChunkCoord center, uint64_t frameIndex,
        std::span<ChunkRecord*, 25> recs)
    {
        using namespace procgen;

        ChunkCoord neighbors[25];
        get5x5Neighborhood(center, neighbors); // inner 3x3 = [0..8]
        std::printf("Building: {%d, %d}\n", center.x, center.z);
        TerrainRenderable* renderable = recs[4]->renderableTarget;
#ifdef M_DEBUG
        std::printf("[buildRenderablev2] center={%d,%d} renderableTarget=%p\n",
                    center.x, center.z, (void*)renderable);
        if (!renderable) {
            std::printf("[buildRenderablev2] ERROR: renderableTarget is null!\n");
            assert(false && "renderableTarget is null");
        }
#endif
        renderable->resetKeepCapacity();
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

        //Logger::log(1, "Completed Packing\n\
        //        Verts Size: %d\n\
        //        IBO Size: %d\n\
        //        TriAdj Size: %d\n\
        //        Packed Positions: %d\n\
        //        Packed Triangles: %d\n\
        //        Total: %d\n",
        //    totalCoreVerts * sizeof(glm::vec3),
        //    renderable->ibo.size() * sizeof(uint32_t),
        //    totalVerts * sizeof(VertexAdjacency),
        //    renderable->packedPositions.size() * sizeof(glm::vec4),
        //    renderable->packedTriangles.size() * sizeof(glm::vec3),
        //    totalCoreVerts * sizeof(glm::vec3) +
        //    renderable->ibo.size() * sizeof(uint32_t) +
        //    totalVerts * sizeof(VertexAdjacency) +
        //    renderable->packedPositions.size() * sizeof(glm::vec4) +
        //    renderable->packedTriangles.size() * sizeof(glm::vec3)
        //);

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
    }

    //bool ChunkManager::tryTakeRenderable(
    //    ChunkCoord center,
    //    uint64_t requestId,
    //    std::unique_ptr<procgen::TerrainRenderable>& out)
    //{
    //    ChunkRecord* rec = getOrCreateRecord(center);
    //    if (!rec) { return false; }

    //    std::scoped_lock lock(rec->renderableMutex);

    //    if (rec->renderableState != RenderableBuildState::Ready) {
    //        return false;
    //    }

    //    if (rec->completedRenderableId != requestId) {
    //        return false; // stale completion or newer request replaced it
    //    }

    //    if (!rec->renderableResult) {
    //        return false;
    //    }

    //    out = std::move(rec->renderableResult);

    //    // Keep or clear state depending on caching policy.
    //    rec->renderableState = RenderableBuildState::None;
    //    rec->renderablePublished = false;

    //    return true;
    //}

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
                        clearSpatialGridReady(rec->coord);
                        clearAllStagesComplete(rec->coord);
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

    bool ChunkManager::evictRecord(ChunkCoord coord) {
        const size_t stripeIdx = stripeIndexwh(coord);
        auto& bucket = records_[stripeIdx];

        std::unique_ptr<ChunkRecord> toFree;
// #region agent log
        auto _ev_t0 = std::chrono::steady_clock::now();
        float _lockMs = 0.f, _clearMs = 0.f;
// #endregion
        {
// #region agent log
            auto _tLock0 = std::chrono::steady_clock::now();
// #endregion
            std::lock_guard<std::mutex> lock(bucket.mut);
// #region agent log
            _lockMs = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-_tLock0).count();
// #endregion
            auto it = bucket.map.find(coord);
            if (it == bucket.map.end()) return false;

            ChunkRecord* rec = it->second.get();
            if (rec->pinCount.load(std::memory_order_relaxed) != 0) return false;

// #region agent log
            auto _tClear0 = std::chrono::steady_clock::now();
// #endregion
            clearSpatialGridReady(coord);
            clearAllStagesComplete(coord);
// #region agent log
            _clearMs = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-_tClear0).count();
// #endregion
            toFree = std::move(it->second);
            bucket.map.erase(it);
        }
// #region agent log
        auto _tDestroy0 = std::chrono::steady_clock::now();
// #endregion
        toFree.reset();
// #region agent log
        auto _tDestroy1 = std::chrono::steady_clock::now();
        float _destroyMs = std::chrono::duration<float,std::milli>(_tDestroy1-_tDestroy0).count();
        float _totalMs = std::chrono::duration<float,std::milli>(_tDestroy1-_ev_t0).count();
        { FILE* _f; fopen_s(&_f, "c:/Users/Yoshi/dev/Midnight/debug-ed8025.log", "a");
          if(_f){ std::fprintf(_f,"{\"sessionId\":\"ed8025\",\"hypothesisId\":\"A\",\"location\":\"ChunkManager.cpp:evictRecord\",\"message\":\"record destroy\",\"data\":{\"coord\":[%d,%d],\"lockMs\":%.3f,\"clearMs\":%.3f,\"destroyMs\":%.3f,\"totalMs\":%.3f},\"timestamp\":%lld}\n",coord.x,coord.z,_lockMs,_clearMs,_destroyMs,_totalMs,(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); std::fclose(_f);} }
// #endregion
        return true;
    }

    int ChunkManager::batchEvictRegion(ChunkCoord center)
    {
// #region agent log
        auto _t0 = std::chrono::steady_clock::now();
// #endregion
        ChunkCoord region[25];
        get5x5Neighborhood(center, region);

        // Phase 1 — Detach records from stripe maps.
        //   Lock each stripe only long enough to find+move+erase.
        //   No nested locks, no global mutex under stripe lock.
        std::unique_ptr<ChunkRecord> detached[25];
        int evicted = 0;
        for (int i = 0; i < 25; ++i) {
            const size_t stripeIdx = stripeIndexwh(region[i]);
            auto& bucket = records_[stripeIdx];
            std::lock_guard<std::mutex> lock(bucket.mut);
            auto it = bucket.map.find(region[i]);
            if (it == bucket.map.end()) continue;
            detached[evicted++] = std::move(it->second);
            bucket.map.erase(it);
        }

// #region agent log
        auto _t1 = std::chrono::steady_clock::now();
// #endregion

        // Phase 2 — Batch-clear global tracking sets (2 lock acquisitions total).
        {
            std::lock_guard<std::mutex> lock(allSpatialGridReadyMut_);
            for (int i = 0; i < 25; ++i)
                allSpatialGridReady_.erase(region[i]);
        }
        {
            std::lock_guard<std::mutex> lock(allStagesCompleteMut_);
            for (int i = 0; i < 25; ++i)
                allStagesComplete_.erase(region[i]);
        }

// #region agent log
        auto _t2 = std::chrono::steady_clock::now();
// #endregion

        // Phase 3 — Destroy detached records outside all locks.
        for (int i = 0; i < evicted; ++i)
            detached[i].reset();

// #region agent log
        auto _t3 = std::chrono::steady_clock::now();
        float _stripesMs = std::chrono::duration<float,std::milli>(_t1-_t0).count();
        float _clearMs   = std::chrono::duration<float,std::milli>(_t2-_t1).count();
        float _destroyMs = std::chrono::duration<float,std::milli>(_t3-_t2).count();
        float _totalMs   = std::chrono::duration<float,std::milli>(_t3-_t0).count();
        { FILE* _f; fopen_s(&_f, "c:/Users/Yoshi/dev/Midnight/debug-ed8025.log", "a");
          if(_f){ std::fprintf(_f,"{\"sessionId\":\"ed8025\",\"runId\":\"batch-fix\",\"hypothesisId\":\"F\",\"location\":\"ChunkManager.cpp:batchEvictRegion\",\"message\":\"batch eviction\",\"data\":{\"coord\":[%d,%d],\"evicted\":%d,\"stripesMs\":%.3f,\"clearMs\":%.3f,\"destroyMs\":%.3f,\"totalMs\":%.3f},\"timestamp\":%lld}\n",center.x,center.z,evicted,_stripesMs,_clearMs,_destroyMs,_totalMs,(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); std::fclose(_f);} }
// #endregion
        return evicted;
    }

}