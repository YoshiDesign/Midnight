#include "ChunkManager.h"
#include "avpch.h"
#include "Runtime/Threading/Scratch.h"
#include "Module/Procgen/Noise/Bluenoise.h"
#include "Module/Procgen/Noise/Functions.h"
#include "Module/Procgen/Delaunay.h"

#ifdef M_DEBUG
#include <fstream>
#include <string>
#include <cstdint>
#include <format>
#include "Runtime/Debug.h"
#endif

namespace aveng {

#ifdef M_DEBUG
#include <filesystem>

    // Creates all of the files for chunk generation debugging
    // Each file is later written from a different location.
    void createDebugFileForChunk(ChunkCoord coord)
    {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        fs::create_directories(exeDir);

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        // Blue Noise
        fs::path fullPath = exeDir /
            std::format("chunk_{}.txt", name);

        // Height field
        fs::path heightPath = exeDir /
			std::format("chunk_{}_heights.txt", name);

		// Triangulation
        fs::path trisPath = exeDir /
            std::format("chunk_{}_tris.txt", name);

        std::ofstream file(fullPath);

        if (!file) {
            throw std::runtime_error("Failed to create file");
        }
    }

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
#endif

    // Get 3x3 neighborhood coordinates (including self at center)
    inline void get3x3Neighborhood(ChunkCoord center, ChunkCoord out[9]) noexcept {
        out[0] = {center.x - 1, center.z - 1};
        out[1] = {center.x,     center.z - 1};
        out[2] = {center.x + 1, center.z - 1};
        out[3] = {center.x - 1, center.z};
        out[4] = {center.x,     center.z};
        out[5] = {center.x + 1, center.z};
        out[6] = {center.x - 1, center.z + 1};
        out[7] = {center.x,     center.z + 1};
        out[8] = {center.x + 1, center.z + 1};
    }

    inline Bounds2 expandBounds(Bounds2 b, float halo) noexcept {
        b.minX -= halo; b.minZ -= halo;
        b.maxX += halo; b.maxZ += halo;
        return b;
    }

    inline bool inBoundsInclusiveMax(const Bounds2& b, float x, float z) noexcept {
        // Use inclusive max to be robust against FP jitter on borders.
        return x >= b.minX && x <= b.maxX && z >= b.minZ && z <= b.maxZ;
    }

    // Quantize to a grid for stable dedupe (works well for floating point).
    // eps should be smaller than your minimum point spacing (e.g. 1e-3 or 1e-4 * world units),
    // and much smaller than any meaningful distances in your simulation.
    struct QKey {
        int64_t qx{};
        int64_t qz{};
        friend bool operator==(QKey a, QKey b) noexcept { return a.qx == b.qx && a.qz == b.qz; }
    };

    // TODO - Template this, it's the same as ChunkCoordHash
    struct QKeyHash {
        size_t operator()(QKey k) const noexcept {
            // 64-bit mix
            uint64_t ux = (uint64_t)k.qx;
            uint64_t uz = (uint64_t)k.qz;
            uint64_t h = (ux << 32) ^ uz;
            h ^= (h >> 33); h *= 0xff51afd7ed558ccdULL;
            h ^= (h >> 33); h *= 0xc4ceb9fe1a85ec53ULL;
            h ^= (h >> 33);
            return (size_t)h;
        }
    };

    inline QKey quantize(const Vec2& p, float eps) noexcept {
        // round to nearest integer grid step
        const float inv = 1.0f / eps;
        return QKey{
            (int64_t)llround((double)p.x * (double)inv),
            (int64_t)llround((double)p.y * (double)inv)
        };
    }

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
        const size_t stripeIdx = hash & (STRIPES - 1); // Use bitwise AND to get the lowest 6 bits - faster than % but only works if STRIPES is a power of two (it is)
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
        rec->coreBounds = {
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
                //auto er = tasks_.wait(requestErosion(rec->coord, frameIndex));

                //auto mesh = buildMesh(*rec);

                //return mesh;
                return nullptr;
            });
        });

        // [!] On the main thread we would use .get() on this future, which blocks.
        return rec->meshF;
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

    //// Erosion (placeholder)
    //std::shared_future<ErosionField const*> ChunkManager::requestErosion(ChunkCoord c, uint64_t frameIndex) {
    //    auto rec = getOrCreateRecord(c);

    //    std::call_once(rec->erosionOnce, [&] {
    //        rec->erosionF = tasks_.submit([this, rec, c]() -> ErosionField const* {
    //            RecordPin taskHold(*this, rec);
    //            return buildErosion(*rec);
    //        });
    //    });

    //    return rec->erosionF;
    //}

    Points const* ChunkManager::buildPoints(ChunkRecord& rec)
    {

        // 1) Reset thread-local scratch for this job
        tlsScratchArena().reset();

        auto* mr = tlsScratchArena().mr();
        assert(mr && "tlsScratch.mr() is null");

        // 2) Generate deterministic seed for this chunk
        uint64_t seed = chunkSeed(cfg_.worldSeed, rec.coord);
#ifdef M_DEBUG
        createDebugFileForChunk(rec.coord);
#endif
        // 3) Generate blue noise points using thread-local scratch
        noise::BlueNoiseConfig bnCfg{};
        bnCfg.MinDist = cfg_.minPointDist; 
        bnCfg.MaxTries = 30;

        auto candidates = GenerateBlueNoiseSeeded(
            static_cast<int64_t>(seed),
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
        collected.reserve(10000); // heuristic: ~1000 points/chunk × 9 neighbors
        
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
        
        // 6) Deduplicate using quantization - This can actually be improved upon!
        constexpr float DEDUPE_EPS = 1e-4f; // smaller than minPointDist
        // std::pmr::unordered_set<QKey, QKeyHash> seen(tlsScratch.mr());
        std::pmr::unordered_set<QKey, QKeyHash> seen(
            0, QKeyHash{}, std::equal_to<QKey>{}, tlsScratchArena().mr()
        );
        seen.reserve(collected.size());
        
        std::pmr::vector<Vec2> unique(tlsScratchArena().mr());
        unique.reserve(collected.size());
        
        for (const auto& pt : collected) {
            QKey key = quantize(pt, DEDUPE_EPS);
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
        heightsOut.resize(rec.allPoints->coreIdx.size());

        // Bring the (default) noise
        noise::NoiseParams np = defaultNoiseParams();

        for (size_t i = 0; i < rec.allPoints->coreIdx.size(); i++) {
            auto idx = rec.allPoints->coreIdx[i];
            std::printf("i: %d\tidx: %d\n", i, idx);
            heightsOut[i] = FractalNoiseV2(
                rec.allPoints->pts[idx].x,
                rec.allPoints->pts[idx].y, // Z - in engine terms, don't confuse this during translation!
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

    // See https://chatgpt.com/share/698e7ab7-08a4-800a-86e1-291e1041e496
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