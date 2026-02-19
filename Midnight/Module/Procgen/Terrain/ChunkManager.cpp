#include "ChunkManager.h"
#include "avpch.h"

#include "Module/Procgen/Noise/Bluenoise.h"

namespace aveng {

    // Get 3x3 neighborhood coordinates (including self at center)
    inline void get3x3Neighbors(ChunkCoord center, ChunkCoord out[9]) noexcept {
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

    // Thread local scratch arena - Tier 1 of our 3-tier arena strategy
    thread_local ChunkArena tlsScratch;

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
        std::printf("%s\n", __FUNCTION__);
        const size_t hash = ChunkCoordHash{}(coord); // turns (x,z) into a size_t
        // Note: This only looks at the lowest 6 bits of the final hash. We use the MurmurHash3 algorithm for this.
        // const size_t stripeIdx = hash % STRIPES; // Determine which bucket's map the record ends up in - index will always be [0, STRIPES)
        const size_t stripeIdx = hash & (STRIPES - 1); // Use bitwise AND to get the lowest 6 bits - faster than % but only works if STRIPES is a power of two
        auto& bucket = records_[stripeIdx];

        // RAII lock_guard
        std::lock_guard<std::mutex> lock(bucket.mut);

        // Insert the key if it's missing, with a null unique_ptr placeholder.
        auto [it, inserted] = bucket.map.try_emplace(coord, nullptr);
        if (!inserted) {
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

        ChunkRecord* out = rec.get();
        it->second = std::move(rec); // "overwrite" the nullptr with the new record
        return out;
    }

    std::shared_future<Points const*> ChunkManager::requestPoints(ChunkCoord c, uint64_t frameIndex)
    {
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->pointsOnce, [this, rec] {
            rec->pointsF = tasks_.submit([this, rec]() -> Points const* {
                RecordPin taskHold(*this, rec);   // pin/unpin
                return buildPoints(*rec);
            });
        });

        return rec->pointsF;
    }

    // AllPoints (depends on 9 point sets)
    std::shared_future<AllPoints const*> ChunkManager::requestAllPoints(ChunkCoord c, uint64_t frameIndex)
    {
        std::printf("%s\n", __FUNCTION__);
        ChunkRecord* rec = getOrCreateRecord(c);
        rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

        std::call_once(rec->allPointsOnce, [this, rec, c, frameIndex] {
            rec->allPointsF = tasks_.submit([this, rec, c, frameIndex]() -> AllPoints const* {
                // Keep the center record alive while we build its AllPoints
                RecordPin selfHold(*this, rec, frameIndex);

                // 3x3 neighborhood around c
                std::array<ChunkCoord, 9> neighbors;
                get3x3Neighbors(c, neighbors.data());

                // Pin neighbors for the duration so their arena-backed Points can't be evicted mid-build.
                std::array<RecordPin, 9> neighborHolds;
                for (int i = 0; i < 9; ++i) {
                    ChunkRecord* nrec = getOrCreateRecord(neighbors[i]);
                    neighborHolds[i] = RecordPin(*this, nrec, frameIndex);
                }

                // Ensure neighbors' points exist
                std::array<std::shared_future<Points const*>, 9> pf;
                for (int i = 0; i < 9; ++i) {
                    std::printf("Requesting points for neighbor: %d\n", i);
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
    //std::shared_future<Heights const*> ChunkManager::requestHeights(ChunkCoord c) {
    //    auto rec = getOrCreateRecord(c);

    //    std::call_once(rec->heightsOnce, [&] {
    //        rec->heightsF = tasks_.submit([this, rec, c]() -> Heights const* {
    //            //auto af = requestAllPoints(c);
    //            //(void)tasks_.wait(af);
    //            //return buildHeights(*rec);
    //        });
    //    });

    //    return rec->heightsF;
    //}

    // Triangulation
    //std::shared_future<Triangulation const*> ChunkManager::requestTriangulation(ChunkCoord c) {
    //    auto rec = getOrCreateRecord(c);

    //    std::call_once(rec->triangOnce, [&] {
    //        rec->triangF = tasks_.submit([this, rec, c]() -> Triangulation const* {
    //            auto hf = requestHeights(c);
    //            (void)tasks_.wait(hf);
    //            return buildTriangulation(*rec);
    //        });
    //    });

    //    return rec->triangF;
    //}

    //// Erosion (placeholder)
    //std::shared_future<ErosionField const*> ChunkManager::requestErosion(ChunkCoord c) {
    //    auto rec = getOrCreateRecord(c);

    //    std::call_once(rec->erosionOnce, [&] {
    //        rec->erosionF = tasks_.submit([this, rec, c]() -> ErosionField const* {
    //            auto tf = requestTriangulation(c);
    //            (void)tasks_.wait(tf);
    //            return buildErosion(*rec);
    //        });
    //    });

    //    return rec->erosionF;
    //}

    // Consider this implementation now that we've reasoned about pinning/unpinning, 
    //std::shared_future<FinalMeshCPU const*> ChunkManager::requestMesh(ChunkCoord c, uint64_t frameIndex)
    //{
    //    ChunkRecord* rec = getOrCreateRecord(c);
    //    rec->lastTouchedFrame.store(frameIndex, std::memory_order_relaxed);

    //    std::call_once(rec->meshOnce, [this, rec] {
    //        rec->meshF = tasks_.submit([this, rec]() -> FinalMeshCPU const* {
    //            RecordPin pipelineHold(*this, rec); // holds across all stages

    //            // These can schedule/use other stages without extra pinning
    //            auto pts = requestPoints(rec->coord, /*frameIndex*/0).get();
    //            auto h = requestHeights(rec->coord, 0).get();
    //            auto er = requestErosion(rec->coord, 0).get();
    //            auto mesh = buildMesh(*rec);

    //            return mesh;
    //        });
    //    });

    //    return rec->meshF;
    //}

    // Final mesh (alloc in `final`) + drop scratch
    //std::shared_future<FinalMeshCPU const*> ChunkManager::requestFinalMesh(ChunkCoord c) {
    //    auto rec = getOrCreateRecord(c);

    //    std::call_once(rec->meshOnce, [&] {
    //        rec->meshF = tasks_.submit([this, rec, c]() -> FinalMeshCPU const* {
    //            auto ef = requestErosion(c);      // or requestTriangulation() if you prefer first
    //            (void)tasks_.wait(ef);

    //            auto* mesh = buildFinalMesh(*rec);

    //            // You can drop scratch now (optional: keep for debug/editor)
    //            rec->discardScratchIntermediates();
    //            return mesh;
    //        });
    //    });

    //    return rec->meshF;
    //}

    void ChunkManager::test(ChunkCoord c, uint64_t frameIndex)
    {
        std::printf("[%d] Begin Test: Requesting All Points for (%s, %s)\n", frameIndex, c.x, c.z);
        auto pf = requestAllPoints(c, frameIndex);
    }

    Points const* ChunkManager::buildPoints(ChunkRecord& rec)
    {
        // 1) Reset thread-local scratch for this job
        tlsScratch.reset();

        // 2) Generate deterministic seed for this chunk
        uint64_t seed = chunkSeed(cfg_.worldSeed, rec.coord);

        // 3) Generate blue noise points using thread-local scratch
        BlueNoiseConfig bnCfg{};
        bnCfg.MinDist = cfg_.minPointDist;
        bnCfg.MaxTries = 30;

        auto candidates = GenerateBlueNoiseSeeded(
            static_cast<int64_t>(seed),
            rec.coreBounds.minX,
            rec.coreBounds.minZ,
            rec.coreBounds.maxX,
            rec.coreBounds.maxZ,
            bnCfg,
            tlsScratch.mr()  // Use thread-local scratch
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
        tlsScratch.reset();
        
        // 2) Allocate AllPoints in chunk scratch (persists across stages)
        if (!rec.allPoints) {
            auto alloc = std::pmr::polymorphic_allocator<AllPoints>(rec.scratch.mr());
            rec.allPoints = alloc.allocate(1);
            std::construct_at(rec.allPoints, rec.scratch.mr());
        }
        
        // 3) Calculate expanded bounds for halo region
        const Bounds2 haloBounds = expandBounds(rec.coreBounds, rec.halo);
        
        // 4) Temporary collection using thread-local scratch
        std::pmr::vector<Vec2> collected(tlsScratch.mr());
        collected.reserve(10000); // heuristic: ~1000 points/chunk × 9 neighbors
        
        // 5) Iterate through 9 neighbors and collect points within halo
        std::array<ChunkCoord, 9> neighbors;
        get3x3Neighbors(rec.coord, neighbors.data());
        
        for (int i = 0; i < 9; ++i) {
            ChunkRecord* nrec = getOrCreateRecord(neighbors[i]);
            
            // Points should exist (pinned and requested in requestAllPoints)
            if (!nrec->points) continue; // defensive
            
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
            0, QKeyHash{}, std::equal_to<QKey>{}, tlsScratch.mr()
        );
        seen.reserve(collected.size());
        
        std::pmr::vector<Vec2> unique(tlsScratch.mr());
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