#include "ChunkManager2.h"

#include <atomic>
#ifdef M_DEBUG
#include <filesystem>
#include <string>
#include <cstdint>
#include <format>
#include "Runtime/Debug.h"
#endif
#include "Utils/Logger.h"
#include "Core/Math/quantize.h"
#include "CoreVK/VkRenderData.h"
#include "Module/Procgen/TerrainArena.h";
#include "Module/Procgen/Noise/Bluenoise.h"
#include "Module/Procgen/Terrain/TerrainPool.h"
#include "Module/Procgen/Terrain/Erosion/Data.h"
#include "Module/Procgen/Terrain/Erosion/ErosionManager.h"
#include "Module/Procgen/Terrain/Erosion/HydraulicErosion.h"
#include "Module/Procgen/Terrain/Erosion/RidgeEnhancement.h"
#include "Module/Procgen/Terrain/Erosion/ThermalErosion.h"

#include "Module/Procgen/Terrain/ChunkRecord2.h"

namespace {

    // Get 3x3 neighborhood coordinates (including self at center)
    void get3x3Neighborhood(procgen::ChunkCoord center, procgen::ChunkCoord out[9]) noexcept {
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
    void get5x5Neighborhood(procgen::ChunkCoord center, procgen::ChunkCoord out[25]) noexcept {
        // Inner 3x3 first (same layout as get3x3Neighborhood)
        get3x3Neighborhood(center, out);
        // Outer ring (16 chunks)
        int idx = 9;
        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                // Omit the central 3x3 check (already filled)
                if (dx >= -1 && dx <= 1 && dz >= -1 && dz <= 1) { continue; }
                out[idx++] = { center.x + dx, center.z + dz };
            }
        }
    }

    procgen::Bounds2 expandBounds(procgen::Bounds2 b, float halo) noexcept {
        b.minX -= halo; b.minZ -= halo;
        b.maxX += halo; b.maxZ += halo;
        return b;
    }

    bool inBoundsInclusiveMax(const procgen::Bounds2& b, float x, float z) noexcept {
        // Use inclusive max to be robust against FP jitter on borders.
        return x >= b.minX && x <= b.maxX && z >= b.minZ && z <= b.maxZ;
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
        coord_to_handle.reserve(MAX_CHUNK_RECORDS * 2);
        coord_to_handle.max_load_factor(0.7f);

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

        coord_to_handle.reserve(MAX_CHUNK_RECORDS);

        // Arena already allocated by Midnight::initialize
        terrain_arena = arena;

        aveng::Logger::log(1, "Allocating Chunk Resources & Resetting in-use flags");

        // Virtual memory allocations
        aveng::ArenaReset(terrain_arena);

        /* Space Allocations */
        scratch_space = (std::byte*)aveng::ArenaAlloc(terrain_arena, MAX_CHUNK_RECORDS * kScratchBytesPerSlot);
        final_space   = (std::byte*)aveng::ArenaAlloc(terrain_arena, MAX_CHUNK_RECORDS * kFinalBytesPerSlot);

        // Establish arena metadata for our trivial lookup types
        for (size_t i = 0; i < terrain_pool->capacity; i++) {

            // Scratch-space descriptors
            terrain_pool->_scratch[i].base = scratch_space + i * kScratchBytesPerSlot;
            terrain_pool->_scratch[i].capacity = kScratchBytesPerSlot;
            terrain_pool->_scratch[i].offset = 0;

            // Final-space descriptors
            terrain_pool->_final[i].base = final_space + i * kFinalBytesPerSlot;
            terrain_pool->_final[i].capacity = kFinalBytesPerSlot;
            terrain_pool->_final[i].offset = 0;

        }

        // Set every chunk's in-use indicator to {false}
        for (auto& flag : terrain_pool->in_use_flag) {
            flag.store(false, std::memory_order_relaxed);
        }

        // Set every chunk's RenderableBuildState flag to {None}
        for (auto& flag : terrain_pool->build_state_flag) {
            flag.store(RenderableBuildState::None, std::memory_order_relaxed);
        }

#ifdef M_DEBUG
        assert(coord_to_handle.size() == 0 && "coord_to_hanlde is not size() == 0!");
#endif

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



    ChunkRecord2* ChunkManager2::getOrCreateRecord(ChunkCoord coord, uint64_t frameIndex = 0)
    {

        /*
        * IMPORTANT: WE ARE NOT RECYCLING HANDLES - they get pop n' swapped, I believe
        *            WE ARE RECYCLING CHUNKRECORDS
        * Things we need to do upon retiring a chunk.
        * - Set handle.active to false
		* - Set rec->active to false
        * - Set pool in-use flag to false
        * - Set pool build state to None
        * - Set handle.index to INVALID_CHUNK_INDEX (see ChunkRecord2.h)
        * - Set rec->index to INVALID_CHUNK_INDEX
        * - Erase [coord] from coord_to_handle - In the future we'll reuse handles + generation
        */

        ChunkHandle handle = coord_to_handle[coord];

        // A new handle was inserted - there is no corresponding chunk record. (invariant)
        if (!handle.active) {

#ifdef M_DEBUG
            assert(frameIndex != 0 && "frameIndex was not provided during handle creation");
#endif

            handle.frameRequested = frameIndex;

            // Too many chunks in memory
            if (coord_to_handle.size() > MAX_CHUNK_RECORDS) { 
                coord_to_handle.erase(coord); // Not thread safe, but logically contained 
                return nullptr; 
            }

            // CAS! 
            bool expected = false;
            if (!terrain_pool->in_use_flag[handle.index].compare_exchange_strong(
                expected,
                true,
                std::memory_order_acquire,
                std::memory_order_relaxed))
            {
                // already owned by another worker
                return nullptr;
            }

            ChunkRecord2* rec = &terrain_pool->records[handle.index];

            // Get - This should, in theory, never typically occur because we already checked handle.active
            if (rec->active == true) { return rec; }

            // Create
            rec->active = true; // TODO - Ensure this is set to `false` upon retire/evict
            rec->index = handle.index;
            rec->generation++; // NOTE - This is just future-proofing. Right now, handles are unique to chunk coord's
            rec->coord = coord;
            rec->halo = cfg_.halo;
            rec->coreBounds = { // Bounds are in world space with local space {0,0} at bottom-left
                 coord.x * cfg_.chunkSize,
                 coord.z * cfg_.chunkSize,
                (coord.x + 1) * cfg_.chunkSize,
                (coord.z + 1) * cfg_.chunkSize
            };

            return rec;
        }
		// Handle is active - Validate and return
        else {

            /* Danger - Zero Validation - Active handles must point to valid chunk record (invariant) */
			return &terrain_pool->records[handle.index];
        
        }
        
    }

    /* Async */
    uint64_t ChunkManager2::requestRenderableAsync(ChunkCoord center, uint64_t frameIndex,
        procgen::TerrainRenderable* target, uint32_t slotIndex)
    {
        // Atomically acquire a free/unused record
        ChunkRecord2* rec = getOrCreateRecord(center);
        if (rec == nullptr) { return INVALID_CHUNK_REQUEST; }

        uint64_t requestId = 0;

        auto& stateAtomic = terrain_pool->build_state_flag[rec->index];

		RenderableBuildState expected = RenderableBuildState::None;
        if (!stateAtomic.compare_exchange_strong(
            expected,
            RenderableBuildState::Queued,
            // These require the invariant: We never request the a coordinate from the streamer more than once (same work across multiple threads).
            std::memory_order_relaxed,  // Safer -> std::memory_order_acq_rel
            std::memory_order_relaxed)) // Safer -> std::memory_order_acquire
        {
            return INVALID_CHUNK_REQUEST;
        }

        /* Alternatively we could punt to load/store with acquire/release but that doesn't guarantee safety from logic race conditions - the invariant holds */

        tasks_.enqueue([this, center, frameIndex, requestId]() {

            // Re-resolve ownership in case this lambda runs long after `rec` is destroyed or repurposed.
            ChunkRecord2* workerRec = getOrCreateRecord(center);
            RecordPin hold(*this, workerRec, frameIndex);

            // Update build state - Again, this will likely change if we ever allow multiple requests per chunk. 
            // Example: we allow multiple workers to perform isolated tasks on a chunk (different request types? E.g. Generate, deform, etc...)
            terrain_pool->build_state_flag[workerRec->index].store(RenderableBuildState::Building, std::memory_order_relaxed);

            runGenerate(center, frameIndex, requestId);
        });
        
        return requestId;
    }

    void ChunkManager2::runGenerate(ChunkCoord center, uint64_t frameIndex, uint64_t requestId)
    {
        ChunkRecord2* centerRec = getOrCreateRecord(center, frameIndex);

        ChunkCoord neighbors[25];
        get5x5Neighborhood(center, neighbors);

        // Pin all 25 records BEFORE readiness checks to prevent
        // evictRecord from destroying them while we hold raw pointers.
        ChunkRecord2* nrecs[25];
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

        /*
        * Hello weary reader. Please accept these architectural notes.
        * Please notice that we do not use CAS unconditionally.
        * Let's try to keep a thread's atomic usage isolated to the chunk it is working on.
		* 
		* If you follow this basic model to generate terrain the god's will smile upon you and your performance will be good.
		* - Build the points for a 5x5 region independently (no CAS, no atomics, just pure worker-local work)
		* - TODO
        * 
        * We might want to reconsider the task graph overall.
        * How much useful work happens per synchronization event?
        *
        * If a stage does thousands of points, triangulation, spatial build, packing, etc. then a few atomics are nothing.
        * If a task does 2 microseconds of work and then reschedules, the scheduler overhead starts dominating.
        * 
        * For a work-stealing deque, the common fast path is:
        * - owner pushes/pops from bottom
        * - thieves rarely steal
        * - most tasks execute locally
        * 
        * So even though the deque uses atomics, the scheduler remains efficient because the ownership model reduces contention.
        * Steals are intentionally more expensive. If stealing becomes frequent, that's a bug stemming from:
        * - poor locality
        * - bad task distribution
        * - tasks too smal
        * - too much dependency churn
        */
        // New Architecture Discussion: https://chatgpt.com/g/g-p-68aca1c346e48191ae5d1ae21818ac34-cpp/c/69e6da7b-c600-83ea-87d9-87893736603e
        /*
        * The Scheduler should be responsible for:
        * - storing runnable tasks
        * - distributing them across workers
        * - load balancing (including how steals happen)
        * - sleeping/waking workers
        * - global injector (external submissions we can store tasks on in addition to each thread-local deque (Chase-Lev))
        *   workers can check the global queue, or alternatively we could round-robin these types of tasks into worker deques.
        * - stealing when local work dries up
        */
        // This separation is important
        /*
		* The Terrain Controller / Streamer should be responsible for:
        * - what the dependencies are
        * - when a chunk becomes worth requesting
        * - what work is legal to start
        * - what fan-out is required
        * - what readiness means
        */
        //
        /*
        * Recommendation from a robot:
        * Do not make the scheduler own the task graph.
        *
        * But also:
        * Do not let arbitrary worker tasks recursively explode the graph too deeply.
        * 
        * Instead, move toward this model:
        * The ChunkManager owns a declarative dependency policy.
        * 
        * A stage request should do one of two things:
        * 
        * 1. Publish missing prerequisites as runnable tasks
        * 2. Return without retry-spamming
        * 
        * Then some other mechanism should reattempt the dependent stage when prerequisites complete, 
        * or at least do so in a controlled way.
        * 
        * That is the key improvement.
        * 
        * Current Architectural Problem: 
        * One thread is not only enqueuing a lot of work, it is also becoming a polling/coordinating thread for the region.
        */
        //
        /*
        * Additional Concerns
        * For one-shot retry flags:
        * - relaxed can sometimes be enough for the flag itself if it is only deduping work
        * - but if you are relying on it as a publication edge, then stronger ordering matters
        * State atomics should be used as publication events, not safety sprinkles
        * 
        * These are the places I’d inspect:
        * 1. Repeated polling loops
        * If runGenerate gets retried often and keeps scanning 25 neighbors repeatedly, 
        * then you may end up doing a lot of "not ready yet" reads
        * 
        * 2. Shared hotspot records
        * The center chunk and its 5x5 neighbors might receive concentrated attention from multiple workers.
        * 
        * 3.Failed CAS frequency
        * A pile of failed CAS attempts across cores is where costs rise
        * 
        * 4. Memory layout of records
        * Possibly the biggest practical performance metric
        * 
        * Measure & Advice:
        * - failed CAS counts
        * - steal rate
        * - retry counts per chunk
        * - Keep tasks chunky
        * - Be selective with cache-line isolation
		* - Prefer publish-once state transitions - e.g. NotStarted->Queued->Ready, rather than toggling flags for retry queuing.
        *   This will reduce churn.
        */

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
                    // This probably doesn't need to happen...
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

    // Points -- no upstream dependency
    void ChunkManager2::requestPoints(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord2* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        auto expected = StageState::NotStarted;
        if (rec->pointsState.compare_exchange_strong(expected, StageState::Queued)) {
            tasks_.enqueue([this, coord = c]() {
                ChunkRecord2* r = getOrCreateRecord(coord);
                RecordPin taskHold(*this, r);
                buildPoints(*r);
                r->pointsState.store(StageState::Ready, std::memory_order_release);
            });
        }
    }

    // AllPoints -- depends on 9 neighbor Points
    void ChunkManager2::requestAllPoints(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord2* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        auto expected = StageState::NotStarted;
        if (rec->allPointsState.compare_exchange_strong(expected, StageState::Queued)) {
            tasks_.enqueue([this, coord = c, frameIndex]() {
                ChunkRecord2* r = getOrCreateRecord(coord);
                runAllPointsStage(*r, frameIndex);
            });
        }
    }

    void ChunkManager2::runAllPointsStage(ChunkRecord2& rec, uint64_t frameIndex) {
        std::array<ChunkCoord, 9> neighbors;
        get3x3Neighborhood(rec.coord, neighbors.data());

        // Pin self + 9 neighbors before readiness checks
        RecordPin selfHold(*this, &rec, frameIndex);
        ChunkRecord2* nrecs[9];
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
                        ChunkRecord2* again = getOrCreateRecord(coord);
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



    Points const* ChunkManager2::buildPoints(ChunkRecord2& rec)
    {
        // Note: this cache-reuse pattern (this `if` statement) is NOT SAFE
        // unless we strictly adhere to the checkerboard request structure.
        // Even then, it's still a high risk pattern. DO NOT LET THREADS
		// READ FROM THE SAME rec.points CONCURRENTLY UNLESS YOU KNOW WHAT YOU ARE DOING.
        // The nullptr check is a cache optimization, not a synchronization mechanism.
        if (rec.points == nullptr) {

            // 1) Scratch memory resource
            ScratchArena& mr = terrain_pool->_scratch[rec.index];
		    mr.offset = 0; // reset for this job

            // 2) Generate the chunk seed - It's used across stages 
            // (and mixed for stochastic determinism. Cool, right?)
            rec.chunkSeed = chunkSeed(cfg_.worldSeed, rec.coord);

            // 3) Generate blue noise points using thread-local scratch
            aveng::noise::BlueNoiseConfig bnCfg{};
            bnCfg.MinDist = cfg_.minPointDist;
            bnCfg.MaxTries = 30;

            PointsRange candidates = aveng::GenerateBlueNoiseSeeded(
                rec.chunkSeed,
                rec.coreBounds.minX,
                rec.coreBounds.minZ,
                rec.coreBounds.maxX,
                rec.coreBounds.maxZ,
                bnCfg,
                mr
#ifdef M_DEBUG
                , rec.coord
#endif
            );

            aveng::Logger::log(1, "[ChunkManager2] Writing Points Output to final storage...");

            // 4) Publish to final arena
            ScratchArena& fmr = terrain_pool->_final[rec.index];

            // Allocate final packed point storage directly into rec.points->core
            rec.points->core = ScratchAlloc<aveng::Vec2>(fmr, candidates.points_size);
            rec.points->size_core = candidates.points_size;

            // Compute offsets for regional bins
            uint32_t begin[BIN_COUNT]{};

            // Bins represent individual regions of points within a chunk
            // This allows neighbors to quickly grab what they need.

            // Build persistent bin metadata directly. Bins are in a fixed order based on cardinal/intercardinal directions
            rec.points->bins[0] = { 0, candidates.binCounts[0] };

            // Record the rest of the metadata
            for (int binIdx = 1; binIdx < BIN_COUNT; ++binIdx) {
                rec.points->bins[binIdx].begin =
                    rec.points->bins[binIdx - 1].begin +
                    rec.points->bins[binIdx - 1].count;

                rec.points->bins[binIdx].count = candidates.binCounts[binIdx];
            }

            // Working write cursors - This holds the write offset we'll use to pack regions contiguously
            uint32_t cursor[BIN_COUNT];
            for (int binIdx = 0; binIdx < BIN_COUNT; ++binIdx) {
                cursor[binIdx] = rec.points->bins[binIdx].begin;
            }

            // Scatter directly into final packed storage using precomputed per-point bins
            for (uint32_t i = 0; i < candidates.points_size; ++i) {
                const aveng::Vec2& p = candidates.points[i];
                const uint8_t bin = candidates.binPerPoint[i];

#ifdef M_DEBUG
                assert(bin < BIN_COUNT && "binPerPoint contained invalid bin");
#endif
                // Packed bin layout -> [SW][S][SE][W][C][E][NW][N][NE]
                // Push the point onto its bin
                rec.points->core[cursor[bin]++] = p;
            }

        }

        return rec.points;
    }

    AllPoints const* ChunkManager2::buildAllPoints(ChunkRecord2& rec) {

         // 1) Scratch memory resource
        ScratchArena& mr = terrain_pool->_scratch[rec.index];
        mr.offset = 0; // reset for this job

        aveng::Vec2* collected = ScratchAlloc<aveng::Vec2>(mr, static_cast<uint32_t>(12000)); // heuristic: ~1000 points/chunk x 9 neighbors
        uint32_t collected_idx = 0;

        // 2) Calculate expanded bounds for halo region
        const Bounds2 haloBounds = expandBounds(rec.coreBounds, rec.halo);

        // 3) Iterate through 9 neighbors and collect points within halo
        std::array<ChunkCoord, 9> neighbors;
        get3x3Neighborhood(rec.coord, neighbors.data());

        for (int i = 0; i < 9; ++i) {
            ChunkRecord2* nrec = getOrCreateRecord(neighbors[i]);

            // Points should exist (pinned and requested in requestAllPoints)
            if (!nrec->points) {
                continue; // defensive
            }

            // Filter points within halo bounds
            // TODO - Pre-compute these during point generation?? We can easily add them to `PointsRange` in a new member
            //      - This would allow us to skip this inner loop
            for (uint32_t j = 0; j < nrec->points->size_core; j++) {
                if (inBoundsInclusiveMax(
                        haloBounds, 
                        nrec->points->core[j].x, 
                        nrec->points->core[j].y)
                    ) {
                    collected[collected_idx++] = nrec->points->core[j];
                }
            }
        }

        // We grab points from generated neighbors, we don't generate halo points for the coord.
        // However, it's still nice to dedupe in case two points are very close along a chunk boundary.
        // 6) Deduplicate using quantization - This can actually be improved upon!
        constexpr float DEDUPE_EPS = 1e-4f;

        // Scratch output buffer: worst case, sized all inclusively
        aveng::Vec2* unique = ScratchAlloc<aveng::Vec2>(mr, collected_idx);
        uint32_t unique_idx = 0; // Final count for uniqueness

        // Open-addressing hash table for QKey dedupe
        // Keep load factor low-ish for cheap probing.
        uint32_t tableCap = 1;
        while (tableCap < collected_idx * 2u) {
            tableCap <<= 1;
        }

        // Table storage
        aveng::QKey* seenKeys = ScratchAlloc<aveng::QKey>(mr, tableCap);
        uint8_t* seenUsed = ScratchAlloc<uint8_t>(mr, tableCap);

        // Mark all slots empty
        std::memset(seenUsed, 0, tableCap * sizeof(uint8_t));

        aveng::QKeyHash hasher{};

        for (uint32_t i = 0; i < collected_idx; ++i) {
            const aveng::Vec2& pt = collected[i];
            const aveng::QKey key = aveng::quantizeFast(pt, DEDUPE_EPS);

            size_t slot = hasher(key) & (tableCap - 1);

            bool alreadySeen = false;
            for (;;) {
                if (!seenUsed[slot]) {
                    // empty slot -> insert
                    seenUsed[slot] = 1;
                    seenKeys[slot] = key;
                    unique[unique_idx++] = pt;
                    break;
                }

                if (seenKeys[slot] == key) {
                    alreadySeen = true;
                    break;
                }

                slot = (slot + 1) & (tableCap - 1);
            }

#ifdef M_DEBUG
            if (alreadySeen) {
                // TODO - Breakpoint here
                aveng::Logger::log(1, "[AllPoints] Already Seen! {%d, %d}\n", seenKeys->qx, seenKeys->qz);
            }
            // [NOTE]
            // We could probably avoid this dedup step entirely if we refuse to generate points
            // within a thin margin around each chunk's boundary. For now it's fast enough
#endif
            // (void)alreadySeen; // optional, only needed if you want clarity/debugging
        }

        // 0) Allocate AllPoints struct in chunk final (persists across stages)
        if (!rec.allPoints) {

			ScratchArena& fmr = terrain_pool->_final[rec.index];
            *rec.allPoints = {
                ScratchAlloc<aveng::Vec2>(fmr, unique_idx), // points
                unique_idx,
                nullptr, // Core indices. Unused
                0        // Size of core indices...
            };

            memcpy(rec.allPoints->all_pts, unique, unique_idx);
        }

        // 7) Identify which points are in core region
        //rec.allPoints->pts.clear(); // DEPRECATED
        //rec.allPoints->pts.reserve(unique.size()); // DEPRECATED
        //rec.allPoints->coreIdx.clear();
        //rec.allPoints->coreIdx.reserve(unique.size() / 9); // ~1/9 are core

        //for (size_t i = 0; i < unique.size(); ++i) {
        //    const auto& pt = unique[i];
        //    rec.allPoints->pts.push_back(pt);

        //    // Check if point is in core bounds (not just halo)
        //    if (rec.coreBounds.contains(pt.x, pt.y)) {
        //        rec.allPoints->coreIdx.push_back(static_cast<uint32_t>(i));
        //    }
        //}

        // 8) Done
        return rec.allPoints;
    }




    bool ChunkManager2::isSpatialGridReady(const ChunkCoord coord) const {
        /*std::lock_guard<std::mutex> lock(allSpatialGridReadyMut_);
        return allSpatialGridReady_.find(coord) != allSpatialGridReady_.end();*/
        return false;
    }

    bool ChunkManager2::isRegionSpatialGridReady(ChunkCoord center) const {
        ChunkCoord region[25];
        get5x5Neighborhood(center, region);

        /*std::lock_guard<std::mutex> lock(allSpatialGridReadyMut_);
        for (int i = 0; i < 25; ++i) {
            if (allSpatialGridReady_.find(region[i]) == allSpatialGridReady_.end())
                return false;
        }*/
        return true;
    }

    bool ChunkManager2::isRegionAllStagesComplete(ChunkCoord center) const {
        ChunkCoord core[9];
        get3x3Neighborhood(center, core);

        //std::lock_guard<std::mutex> lock(allStagesCompleteMut_);
        //for (int i = 0; i < 9; ++i) {
        //    if (allStagesComplete_.find(core[i]) == allStagesComplete_.end())
        //        return false;
        //}
        return true;
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
        // TODO - Use rec->index in the pool to update the pin count
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