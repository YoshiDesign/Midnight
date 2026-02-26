#include "HydraulicErosion.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include "Core/Math/Math.h"
#include "Core/Math/Vector.h"
// #include "Runtime/Threading/Scratch.h"
// #include "Runtime/Memory/ChunkArena.h" // Including this here causes a TU mega-collision
#include "Module/Procgen/Rng.h"
#include "Module/Procgen/Types.h"
#include "Module/Procgen/Delaunay.h"
#include "Module/Procgen/SpatialGrid.h"
#include "Runtime/Threading/ITaskSystem.h"
#include "Module/Procgen/Terrain/ChunkRecord.h"
#include "Module/Procgen/Terrain/Erosion/Data.h"

/*
TODO: Hydraulic Erosion Optimizations
* - Tile sparse accumulation (best overall)
*   Each batch accumulates into a small set of tiles (or per-tile arrays)
*   Reduction only touches tiles that were hit
* -Per-thread (not per-batch) deltas
*   Instead of 30 full deltas, have W full deltas (one per worker thread), reuse them
*   Each task writes into its thread’s delta
*   Then reduce W deltas (W << numBatches)
*   Still bandwidth heavy, but much less than 30×N
*/

namespace procgen {

    // ---- Droplet ----
    struct Droplet {
        aveng::Vec2 pos;    // WORLD x/z
        aveng::Vec2 dir;    // unit direction in x/z
        float water = 0.f;
        float vel = 0.f;
        float sediment = 0.f;
        float capacity = 0.f;
        bool alive = true;
    };

}

namespace procgen::detail {

    // Weighted average hardness on a triangle
    static float AvgHardnessTri(
        std::span<const float> hardness,
        aveng::SiteIndex a, 
        aveng::SiteIndex b, 
        aveng::SiteIndex c,
        const aveng::BaryWeights& w
    )
    {
        // safety: clamp weights if your barycentric can slightly overshoot
        const float wa = w.wa, wb = w.wb, wc = w.wc;
        return wa * hardness[a] + wb * hardness[b] + wc * hardness[c];
    }

    // Move direction update (unit step length preserved)
    static void UpdateDirectionUnitStep(procgen::Droplet& d, aveng::Vec2 gradDownhill, const aveng::HydraulicErosionParams& cfg) {
        // Paper-style: dirNew = dirOld*inertia - grad*(1-inertia), then normalize (magnitude 1)
        const float pin = cfg.inertia;
        aveng::Vec2 dirNew{
            d.dir.x * pin - gradDownhill.x * (1.0f - pin),
            d.dir.y * pin - gradDownhill.y * (1.0f - pin),
        };
        d.dir = dirNew.normalizedOr(d.dir);
    }

}

/*
* Notes from the pro's:
* More advanced hydraulic variants might include:
*    small "erosion brush" neighbor lists
*    triangle/vertex adjacency fetch buffers (wait... come back here after reviewing the adjacency work that's on deck.)
*    per-step visited sets / queues
*    tiny vectors for "affected vertices" when doing localized accumulation
* 
* TLS Scratch becomes essential here.
* 
* [Optimizations]
* 1. tiling/sparse accumulation
*/

namespace procgen {

    // ---- The actual pass ----
    void ComputeHydraulicErosion(
        procgen::ErosionWorkingSet& ws,
        const aveng::AllPoints& allPts,
        const aveng::Triangulation& tri,
        const aveng::SpatialGrid& sg,
        const aveng::HydraulicErosionParams& cfg,
        uint64_t hydroSeed,
        aveng::ITaskSystem& tasks
    ) {
        const size_t N = allPts.pts.size(); // assuming AllPoints::pts is a vector<Vec2> in WORLD space
        if (N == 0) return;

        // Ensure scratch buffers are sized
        ws.delta.assign(N, 0.0f);
        if (ws.workHeights.size() != N) {
            // caller typically pre-fills workHeights from HeightField; this is defensive
            ws.workHeights.resize(N, 0.0f);
        }
        if (ws.hardness.size() != N) {
            // hardness should be computed before this pass; defensive default
            ws.hardness.resize(N, 0.0f);
        }

        const auto bounds = sg.worldBounds;
        const float margin = std::max(0.0f, cfg.spawnMargin);

        const float spawnMinX = bounds.minX + margin;
        const float spawnMaxX = bounds.maxX - margin;
        const float spawnMinZ = bounds.minZ + margin;
        const float spawnMaxZ = bounds.maxZ - margin;

        // If margin is too large, fall back to full bounds (don’t crash / don’t spawn NaNs)
        const bool spawnOk = (spawnMaxX > spawnMinX) && (spawnMaxZ > spawnMinZ);
        const float aMinX = spawnOk ? spawnMinX : bounds.minX;
        const float aMaxX = spawnOk ? spawnMaxX : bounds.maxX;
        const float aMinZ = spawnOk ? spawnMinZ : bounds.minZ;
        const float aMaxZ = spawnOk ? spawnMaxZ : bounds.maxZ;

        // Work constants
        const float oneMinusEvap = 1.0f - cfg.pEvaporation;

        // Batch scheduling
        const uint32_t total = cfg.numDroplets;

        // Hard cap on hydraulic parallelism
        const uint32_t maxTasks = cfg.maxWorkers;

        // Don’t spawn more tasks than droplets
        const uint32_t numBatches = std::min(maxTasks, total);

        // Now derive batch size from desired task count
        const uint32_t batchSize = (total + numBatches - 1u) / numBatches;

        // Each task returns a full-size local delta array (simple + deterministic; optimize later with tiling/sparse).
        std::vector<std::shared_future<std::vector<float>>> futures;
        futures.reserve(numBatches);

        // Capture read-only spans for speed
        const float* heights = ws.workHeights.data();
        const float* hard = ws.hardness.data();

        for (uint32_t b = 0; b < numBatches; ++b) {
            const uint32_t begin = b * batchSize;
            const uint32_t end = std::min(total, begin + batchSize);

            // TODO : I don't think we're using thread local scratch allocation
            futures.push_back(tasks.submit([=, &allPts, &tri, &sg, &cfg]() -> std::vector<float> {

                /*
                    TODO: Debugging
                    Use null_memory_resource() to catch heap allocation
                */

                // Get per-worker scratch
                //aveng::ChunkArena& arena = aveng::tlsScratchArena();
                //if (!arena.mr()) {
                //    // once per thread; pick a size you like
                //    arena.reserve(3 * 1024 * 1024); // example: 3MB
                //}
                //arena.reset(); // IMPORTANT: wipe previous task allocations on this worker

                //std::pmr::memory_resource* mr = arena.mr();

                std::vector<float> localDelta;
                localDelta.assign(allPts.pts.size(), 0.0f);

                for (uint32_t di = begin; di < end; ++di) {
                    aveng::SplitMix64 rng(aveng::cheapMix(hydroSeed, uint64_t(di)));

                    Droplet d;
                    d.water = cfg.initWater;
                    d.vel = cfg.initVel;
                    d.sediment = 0.0f;
                    d.capacity = cfg.pCapacity;
                    d.alive = true;

                    // Spawn position (WORLD) with adjustable margin
                    const float rx = rng.nextFloat01();
                    const float rz = rng.nextFloat01();
                    d.pos = { aMinX + (aMaxX - aMinX) * rx,
                              aMinZ + (aMaxZ - aMinZ) * rz };

                    // Random initial direction (unit)
                    aveng::Vec2 rdir{ rng.nextFloat01() * 2.0f - 1.0f, rng.nextFloat01() * 2.0f - 1.0f };
                    d.dir = rdir.normalizedOr(aveng::Vec2{ 1,0 });

                    for (uint32_t step = 0; step < cfg.maxSteps; ++step) {
                        if (!d.alive) { break; }

                        // Locate current triangle
                        auto [t1, ok1] = sg.LocateTriangle(d.pos.x, d.pos.y);
                        if (!ok1) { d.alive = false; break; }

                        // Barycentric at current position
                        aveng::BaryWeights w1{};
                        if (!aveng::Barycentric(allPts, tri, t1, d.pos, w1)) { d.alive = false; break; }

                        // Sample height at current position
                        float h1 = 0.f;
                        if (!aveng::SampleScalar(allPts, tri, t1, d.pos, heights, allPts.pts.size(), h1)) {
                            d.alive = false; break;
                        }

                        // Triangle gradient (constant over triangle)
                        float dhdx = 0.f, dhdz = 0.f;
                        if (!aveng::TriangleGradient(allPts, tri, t1, heights, allPts.pts.size(), dhdx, dhdz)) {
                            d.alive = false; break;
                        }

                        // Gradient points uphill; we want downhill motion, so gradDownhill = (-dhdx, -dhdz)
                        aveng::Vec2 gradDownhill{ -dhdx, -dhdz };
                        detail::UpdateDirectionUnitStep(d, gradDownhill, cfg);

                        // Unit step (paper intent): pos += dir
                        const aveng::Vec2 oldPos = d.pos;
                        d.pos = { d.pos.x + d.dir.x, d.pos.y + d.dir.y };

                        // Locate new triangle after move
                        auto [t2, ok2] = sg.LocateTriangle(d.pos.x, d.pos.y);
                        if (!ok2) { d.alive = false; break; }

                        // Sample height at new position
                        float h2 = 0.f;
                        if (!aveng::SampleScalar(allPts, tri, t2, d.pos, heights, allPts.pts.size(), h2)) {
                            d.alive = false; break;
                        }
                        
                        // Barycentric at new position (only needed for uphill logic; cheap enough to compute once)
                        aveng::BaryWeights w2{};
                        if (!aveng::Barycentric(allPts, tri, t2, d.pos, w2)) {
                            d.alive = false; break;
                        }
                        
                        const float hdiff = h2 - h1; // <0 downhill

                        // Tri vertex indices
                        const auto& T1 = tri.tris[t1];
                        const auto& T2 = tri.tris[t2];
                        const aveng::SiteIndex A1 = T1.A, B1 = T1.B, C1 = T1.C;
                        const aveng::SiteIndex A2 = T2.A, B2 = T2.B, C2 = T2.C;

                        // ----- Erode / deposit -----
                        if (hdiff < 0.0f) {
                            // Capacity = max(-hdiff, minSlope) * vel * water * pCapacity
                            const float slopeTerm = std::max(-hdiff, cfg.pMinSlope);
                            d.capacity = slopeTerm * d.vel * d.water * cfg.pCapacity;

                            bool excess = false;
                            float toDeposit = 0.f;
                            float toErode = 0.f;

                            if (d.sediment > d.capacity) {
                                toDeposit = (d.sediment - d.capacity) * cfg.pDeposition;
                                d.sediment -= toDeposit;
                                excess = true;
                            }

                            if (d.capacity > d.sediment) {
                                toErode = (d.capacity - d.sediment) * cfg.pErosion;

                                // Clamp by local relief (prevents “pulling” more than descended this step)
                                toErode = std::min(toErode, -hdiff);

                                // Hardness scaling: harder erodes less (hardness assumed [0..1])
                                const float avgH = detail::AvgHardnessTri(std::span<const float>(hard, allPts.pts.size()),
                                   A1, B1, C1, w1);
                                toErode *= (1.0f - avgH);

                                d.sediment += toErode;
                                excess = false;
                            }

                            if (excess) {
                                localDelta[A1] += w1.wa * toDeposit;
                                localDelta[B1] += w1.wb * toDeposit;
                                localDelta[C1] += w1.wc * toDeposit;
                            }
                            else {
                                localDelta[A1] -= w1.wa * toErode;
                                localDelta[B1] -= w1.wb * toErode;
                                localDelta[C1] -= w1.wc * toErode;
                            }
                        }
                        else if (hdiff > 0.0f) {
                            // Uphill: deposit to fill. (Prototype logic preserved.)
                            if (hdiff > d.sediment) {
                                // Drop all sediment at source triangle
                                localDelta[A1] += w1.wa * d.sediment;
                                localDelta[B1] += w1.wb * d.sediment;
                                localDelta[C1] += w1.wc * d.sediment;
                                d.alive = false;
                            }
                            else {
                                const float deposit = std::min(d.sediment, hdiff);
                                localDelta[A1] += w1.wa * deposit;
                                localDelta[B1] += w1.wb * deposit;
                                localDelta[C1] += w1.wc * deposit;
                                d.sediment -= deposit;

                                if (d.sediment < 1e-6f) {
                                    d.sediment = 0.f;
                                    d.vel = 0.f;
                                    // keep direction as-is; if you want “settling”, you can zero dir
                                }
                            }
                        }
                        else {
                            // Flat: no material change (but still evaporate / possibly kill)
                        }

                        // ----- Velocity + evaporation -----
                        // Downhill accelerates: use (-hdiff)
                        d.vel = std::sqrt(std::max(0.0f, d.vel * d.vel + (-hdiff) * cfg.gravity));

                        // Base evaporation
                        d.water *= oneMinusEvap;

                        // Extra evaporation if slope ~ 0 and capacity ~ 0 (requested)
                        // Use gradient magnitude as a stronger flatness signal than hdiff alone.
                        const float slopeMag = std::sqrt(dhdx * dhdx + dhdz * dhdz);
                        if (slopeMag < cfg.flatSlopeEps && d.capacity < cfg.flatCapEps) {
                            d.water *= (1.0f - cfg.flatExtraEvap); // "drastically increase"
                        }

                        if (d.water < 1e-6f) {
                            d.alive = false;
                            break;
                        }

                        // Optional: if we failed to move meaningfully, prevent infinite "sticking"
                        // (unit step avoids this, so likely unnecessary)
                        (void)oldPos;
                        (void)A2; (void)B2; (void)C2; // T2 indices reserved for future inter-tri deposition variants
                    }
                }

                return localDelta;
            }));
        }

        // Reduce all local deltas into ws.delta (single-threaded reduction for now)
        // (You can parallelize the reduction later by chunking the index range.)
        for (auto& fut : futures) {
             /* std::vector<float> */ auto local = fut.get(); // auto might cause a move
            // accumulate
            for (size_t i = 0; i < N; ++i) {
                ws.delta[i] += local[i];
            }
        }

        //// Apply delta into workHeights (in-place)
        //for (size_t i = 0; i < N; ++i) {
        //    ws.workHeights[i] += ws.delta[i];
        //}
    }

}