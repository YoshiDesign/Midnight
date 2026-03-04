#include "ChunkManager.h"
#include "avpch.h"
#include "Core/Math/quantize.h"
#include "Runtime/Threading/Scratch.h"
#include "Module/Procgen/Noise/Bluenoise.h"
#include "Module/Procgen/Noise/Functions.h"
#include "Module/Procgen/Delaunay.h"
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

namespace {

    // TODO - Idk where to put this yet. I'm sure we'll land on a convention
    // as procgen grows.
    void ApplyDelta(std::span<float> work, std::span<const float> delta) {
#ifdef M_DEBUG
        assert(work.size() == delta.size() && "ApplyDelta: size mismatch");
#endif
        for (size_t i = 0; i < work.size(); ++i) {
            work[i] += delta[i];
        }

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
            std::format("chunk_{}_heights.txt", name);

        Debug::writeHeightDataToFile(fullPath, data);
    }

    void dumpTriangulationDatas(ChunkCoord coord, Triangulation* tri_data) {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("chunk_{}_triangulation.txt", name);

		Debug::writeTriangulationDataToFile(fullPath, tri_data);

    }

    void dumpSpatialGridData(ChunkCoord coord, const SpatialGrid* grid) {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("chunk_{}_sgrid.txt", name);

        Debug::writeSgridDataToFile(fullPath, grid);

    }

    void dumpHydraulicData(ChunkCoord coord, const procgen::ErosionWorkingSet* ws) {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("chunk_{}_hydro.txt", name);

        Debug::writeHydroDataToFile(fullPath, ws->delta);

    }

    void dumpThermalData(ChunkCoord coord, const procgen::ErosionWorkingSet* ws) {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("chunk_{}_hydro.txt", name);

        Debug::writeThermalDataToFile(fullPath, ws->delta);

    }

    // Height data writer for debugging
    void dumpChunkFinalHeightData(ChunkCoord coord, std::span<float> data)
    {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("chunk_{}_FinalHeights.txt", name);

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
            std::printf("Pinning chunk record for ChunkCoord(%d, %d)\n", r->coord.x, r->coord.z);
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
    /* Manager Setups */

    /*
    * Policy 
    * - pointers returned by futures are only valid while the chunk is pinned.
    * - Each stage function should only request the minimum prerequisite stage(s).
    * 
    * Design Notes:
    * - Anything you publish (return from a future) must be allocated in rec.final
    * - Scratch is per-thread and reset every job without risking published pointers
    * 
    * Threads:
    * Each OS thread gets its own independent instance of tlsScratch.
    * - When your worker threads run tasks, each worker uses its own scratch arena.
    * - No locking, no contention, no cross-thread memory reuse bugs.
    * - If you ever run tasks on the main thread too (e.g. "helping wait" executes work inline), 
    *   the main thread will have its own tlsScratch instance as well.
    * 
    * Safety:
    *   - pin() is coupled to ChunkRecord generation, but not the other way around. 
    *     We're using a strict lifetime policy at the moment.
    * 
    * We're going to only pin once to protect the entirety of chunk generation.
    * Why might we want to pin an individual stage? yOU AsK?
    * Only if stages can be requested independently and you want those stage tasks to 
    * be safe even when no higher-level "pipeline pin" exists.
    * 
    * We only "pipeline pin" because we want everything to occur before eviction becomes safe.
    * 
    * Future Updates:
    * C) Generate halo points deterministically without reading neighbors
    *       Sometimes people avoid halo stitching by generating points from a world seed for the expanded region and then 
    *       selecting the core subset per chunk. That’s elegant but changes your architecture: each chunk 
    *       would be responsible for generating points in its expanded bounds, which you explicitly said you don’t want.
    */

    /*
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
    ChunkRecord* ChunkManager::getOrCreateRecord(ChunkCoord coord)
    {
        std::printf("%s [%d, %d]\n", __FUNCTION__, coord.x, coord.z);
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
            std::printf("%s Record Already Created for (%d, %d)...\n", __FUNCTION__, coord.x, coord.z);
            return it->second.get();
        }

        // Create a new record - still holding the lock
        auto rec = std::make_unique<ChunkRecord>();
        rec->coord = coord;
        rec->halo = cfg_.halo;
        rec->coreBounds = { // Bounds are in world space
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
    std::shared_future<FinalMeshCPU const*> ChunkManager::requestMesh(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->meshOnce, [this, rec, frameIndex] {
            rec->meshF = tasks_.submit([this, rec, frameIndex]() -> FinalMeshCPU const* {

                RecordPin pipelineHold(*this, rec); // holds across all stages

                // [IMPORTANT]
                // `wait` calls .get() internally while ensuring no pool starves. Typical helping-wait design.
                // 
                // Note that these local future values arent "used", they're mostly just for signaling completion.
                // But, they keep the pipeline honest in case a consumer ever needs to rely on the restulting
                // data without first referencing the chunk record and hoping it's all there.
                // This also forces some obvious invariance: "future is ready so the product exists"
                // 
                // You are the dependency manager here. Invariants are implicit to the weary observer.
                // There's nothing preventing us from requesting heights without having generated points first.
                // We sequentially list stages in order here, instead of wiring stages together explicitly.

                // TODO - Add asserts to each stage to validate prerequisites if we decide to keep this approach
                auto pts = tasks_.wait(requestAllPoints(rec->coord, frameIndex));
                auto h = tasks_.wait(requestHeights(rec->coord, frameIndex));
                auto tri = tasks_.wait(requestTriangulation(rec->coord, frameIndex));
                auto spa = tasks_.wait(requestSpatialGrid(rec->coord, frameIndex));
                auto er = tasks_.wait(requestErosion(rec->coord, frameIndex));

                auto mesh = buildMesh(*rec);

                //return mesh;
                return nullptr;
            });
        });

        // [!] On the main thread we would use .get() on this future, which blocks.
        return rec->meshF;
    }


    ChunkManager::ChunkManager(ThreadPoolTaskSystem& tasks)
        : tasks_(tasks) 
    {
        cfg_ = defaultTerrainConfig(); // Global Config
    }

    std::shared_future<Points const*> ChunkManager::requestPoints(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->pointsOnce, [this, rec] {
            rec->pointsF = tasks_.submit([this, rec]() -> Points const* {
                RecordPin taskHold(*this, rec);   // pin/unpin NOTE: This doesn't necessarily need to happen per stage request since
                // we pin the entirety of the `requestMesh` task. However, I'm keeping it here for now to remind me that this is
				// necessary should we decide to make stages self sufficient in the future.
                // It's necessary either way, in the case of requestAllPoints where we read neighbor data; we pin each neighbor.
                // "neighbor sensitive" stages should pin their neighboring chunks.
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
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->allPointsOnce, [this, rec, c, frameIndex] {
            rec->allPointsF = tasks_.submit([this, rec, c, frameIndex]() -> AllPoints const* {
                // Keep the center record alive while we build its AllPoints
                RecordPin selfHold(*this, rec, frameIndex);

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
                    // std::printf("Requesting points for neighbor: %d\n", i);
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
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->heightsOnce, [&] {
            rec->heightsF = tasks_.submit([this, rec, c, frameIndex]() -> HeightField const* {
                RecordPin taskHold(*this, rec);   // pin/unpin
                return buildHeights(*rec);
            });
        });

        return rec->heightsF;
    }

    // Triangulation
    std::shared_future<Triangulation const*> ChunkManager::requestTriangulation(ChunkCoord c, uint64_t frameIndex) {
        auto rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->triangOnce, [&] {
            rec->triangF = tasks_.submit([this, rec, c, frameIndex]() -> Triangulation const* {
                RecordPin taskHold(*this, rec);   // pin/unpin
                return buildTriangulation(*rec);
            });
        });

        return rec->triangF;
    }

    std::shared_future<SpatialGrid const*> ChunkManager::requestSpatialGrid(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->spatialOnce, [this, rec, frameIndex] {
            rec->spatialF = tasks_.submit([this, rec, frameIndex]() -> SpatialGrid const* {
                RecordPin pin(*this, rec);
                return buildSpatialGrid(*rec); // publishes pointer into rec->spatial
            });
        });

        return rec->spatialF;
    }

    // Erosion
    std::shared_future<ErosionField const*> ChunkManager::requestErosion(ChunkCoord c, uint64_t frameIndex) {
        auto rec = getOrCreateRecord(c);

        std::call_once(rec->erosionOnce, [this, rec, frameIndex] {

            // Get settings
            const ErosionSettings s = erosionMgr_ ? erosionMgr_->getActiveSettings() : ErosionSettings{};

            rec->erosionF = tasks_.submit([this, rec, s, frameIndex]() -> ErosionField const* {
                RecordPin taskHold(*this, rec);
                return buildErosion(*rec, s);
            });

        });

        return rec->erosionF;
    }

    Points const* ChunkManager::buildPoints(ChunkRecord& rec)
    {

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
        // 1) Reset thread-local scratch for this job
        tlsScratchArena().reset();

        // Scratch setup
        auto* mr = tlsScratchArena().mr();
        assert(mr && "tlsScratch.mr() is null");

        std::pmr::vector<float> heightsOut(mr);
        heightsOut.resize(rec.allPoints->pts.size());

        // Bring the (default) noise
        noise::NoiseParams np = defaultNoiseParams();

        for (size_t i = 0; i < rec.allPoints->pts.size(); i++) {
            // Heights in parallel with points (Sites)
            heightsOut[i] = FractalNoiseV2(
                rec.allPoints->pts[i].x,
                rec.allPoints->pts[i].y, // Z - in engine terms
                np);
        }

#ifdef M_DEBUG
		dumpChunkHeightData(rec.coord, heightsOut);
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

        std::printf("Building Triangulation\n");

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
        assert(tri->triEdge0.size() == tri->tris.size());
        assert(tri->siteEdge.size() == static_cast<size_t>(vertexCount));
        // If you do 3 half-edges per triangle:
        // assert(tri->halfEdges.size() == tri->tris.size() * 3);

        dumpTriangulationDatas(rec.coord, rec.triangulation);
#endif
        std::printf("Triangulation Complete\n");
        return rec.triangulation;
    }

    SpatialGrid const* ChunkManager::buildSpatialGrid(ChunkRecord& rec)
    {
#ifdef M_DEBUG
        assert(rec.triangulation && "[buildSpatialGrid] Missing triangulation prerequisite");
        assert(rec.allPoints && "[buildSpatialGrid] Missing allPoints prerequisite");
        assert(rec.heightField && "[buildSpatialGrid] Missing heightField prerequisite");
#endif

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
        {
            std::printf("[SpatialGrid] prereqs: tri=%p pts=%p hf=%p\n",
                (void*)rec.triangulation, (void*)rec.allPoints, (void*)rec.heightField);

            if (rec.triangulation) {
                std::printf("[SpatialGrid] tri: tris=%zu halfEdges=%zu triEdge0=%zu siteEdge=%zu\n",
                    rec.triangulation->tris.size(),
                    rec.triangulation->halfEdges.size(),
                    rec.triangulation->triEdge0.size(),
                    rec.triangulation->siteEdge.size());
            }
            if (rec.allPoints) {
                std::printf("[SpatialGrid] pts: pts=%zu coreIdx=%zu\n",
                    rec.allPoints->pts.size(),
                    rec.allPoints->coreIdx.size());
            }
            if (rec.heightField) {
                std::printf("[SpatialGrid] hf: heights=%zu\n",
                    rec.heightField->heights.size());
            }

            std::printf("[SpatialGrid] cellSize=%f bounds(minx,minz,maxx,maxz)=(%f,%f,%f,%f) halo=%f\n",
                cellSize, minX, minZ, maxX, maxZ, rec.halo);

            if (!(cellSize > 0.0f)) std::printf("[SpatialGrid][!!] cellSize is not > 0\n");
            if (!(maxX > minX && maxZ > minZ)) std::printf("[SpatialGrid][!!] bounds are degenerate/inverted\n");
        }
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

#ifdef M_DEBUG

        if (!sg) {
            std::printf("[SpatialGrid][!!] BuildSpatialGrid returned nullptr\n");
            // If you want to hard-fail but still see the message:
            assert(false && "BuildSpatialGrid returned null (see console prereq dump above)");
        }

        assert(sg && "[buildSpatialGrid] BuildSpatialGrid returned null");
        std::printf("Completed SpatialGrid\n");
#endif

        // Publish into the record. This makes the lifetime tied to ChunkRecord,
        // but still easy to drop/clear on eviction.
        rec.spatial.emplace(std::move(*sg));
        dumpSpatialGridData(rec.coord, &(*rec.spatial));
        // Return stable pointer into the optional.
        return &(*rec.spatial);

    }

    ErosionField const* ChunkManager::buildErosion(ChunkRecord& rec, const ErosionSettings& settings)
    {
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
        (void)requestHeights(rec.coord, 0 /*frameIndex*/).get(); // ensure built
        (void)requestAllPoints(rec.coord, 0 /*frameIndex*/).get(); // ensure built
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

        // 2) Hardness in scratch
        procgen::ComputeHardnessMap(
            ws.hardness, 
			rec.allPoints->pts, // INVARIANT REMINDER - allPoints->pts.size() == heightField->heights.size()
            ws.workHeights,
            settings.hardness,
            hardnessSeed
        );

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

#ifdef M_DEBUG
        dumpHydraulicData(rec.coord, &ws);
#endif

        ApplyDelta(ws.workHeights, ws.delta);
        std::fill(ws.delta.begin(), ws.delta.end(), 0.0f);

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

        // 5) Ridge enhancement can use ping-pong:
        ws.ping.resize(N);
        ComputeRidgeEnhancement( /* WARNING: I DID NOT AUDIT THIS CODE AT ALL */
            ws,
            *rec.allPoints,
            *rec.triangulation,
            rec.spatial.value(),
            settings.ridges,
            ridgeSeed,
            tasks_
        );
        ws.workHeights.swap(ws.ping);

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
        dumpChunkFinalHeightData(rec.coord, rec.erosion->eHeights);
#endif

        // 6) Done. Scratch can be reused immediately by this worker for the next job.
        return rec.erosion;

    }

    FinalMeshCPU const* ChunkManager::buildMesh(ChunkRecord& r)
    {
        // Punt to the BasicTerrainAsset
        return nullptr;
    }

    /* Lifetime saftey features below */

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
                        toFree.emplace_back(std::move(it->second));
                        it = bucket.map.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            } // unlock stripe before freeing memory

            // Destructors run here without holding the stripe lock.
            // `toFree` goes out of scope and frees arenas/buffers.
        }
    }

}