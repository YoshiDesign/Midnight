#include "HydraulicErosion.h"
#include <algorithm>
#include <cstdint>
#include <span>
#include "Core/Math/Math.h"
#include "Core/Math/Vector.h"
#include "Module/Procgen/Rng.h"
#include "Module/Procgen/Types.h"
#include "Module/Procgen/Delaunay.h"
#include "Module/Procgen/SpatialGrid.h"
#include "Runtime/Threading/ITaskSystem.h"
#include "Module/Procgen/Terrain/ChunkRecord.h"
#include "Module/Procgen/Terrain/Erosion/Data.h"
#include "Core/Math/wyhash.h"

/*
* TODO: Hydraulic Erosion Optimizations
* - Tile sparse accumulation (best overall)
*   Each batch accumulates into a small set of tiles (or per-tile arrays)
*   Reduction only touches tiles that were hit
* -Per-thread (not per-batch) deltas
*   Instead of 30 full deltas, have W full deltas (one per worker thread), reuse them
*   Each task writes into its thread's delta
*   Then reduce W deltas (W << numBatches)
*   Still bandwidth heavy, but much less than 30×N
*/

using FPTYPE = float;

namespace procgen {

    // ---- Droplet ----
    struct Droplet {
        aveng::Vec2 pos;    // WORLD x/z
        aveng::Vec2 dir;    // unit direction in x/z
        FPTYPE water = 0.f;
        FPTYPE vel = 0.f;
        FPTYPE sediment = 0.f;
        FPTYPE capacity = 0.f;
        bool alive = true;
    };

}

namespace procgen::detail {

    // Weighted average hardness on a triangle
    FPTYPE AvgHardnessTri(
        std::span<const FPTYPE> hardness,
        aveng::SiteIndex a, 
        aveng::SiteIndex b, 
        aveng::SiteIndex c,
        const aveng::BaryWeights& w
    )
    {
        const FPTYPE wa = w.wa, wb = w.wb, wc = w.wc;
        return wa * hardness[a] + wb * hardness[b] + wc * hardness[c];
    }

    // Move direction update (unit step length preserved)
    void UpdateDirectionUnitStep(procgen::Droplet& d, aveng::Vec2 gradDownhill, const aveng::HydraulicErosionParams& cfg) {
        const FPTYPE pin = cfg.inertia;
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

    std::vector<std::shared_future<std::vector<float>>> SubmitHydraulicBatches(
        procgen::ErosionWorkingSet& ws,
        const aveng::AllPoints& allPts,
        const aveng::Triangulation& tri,
        const aveng::SpatialGrid& sg,
        const aveng::HydraulicErosionParams& cfg,
        uint64_t hydroSeed,
        aveng::ITaskSystem& tasks
    ) {
        const size_t N = allPts.pts.size();

        ws.delta.assign(N, 0.0f);
        if (ws.workHeights.size() != N) {
            ws.workHeights.resize(N, 0.0f);
        }
        if (ws.hardness.size() != N) {
            ws.hardness.resize(N, 0.0f);
        }

        std::vector<std::shared_future<std::vector<FPTYPE>>> futures;
        if (N == 0) return futures;

        const auto bounds = sg.worldBounds;
        const FPTYPE margin = std::max(0.0f, cfg.spawnMargin);

        const FPTYPE spawnMinX = bounds.minX + margin;
        const FPTYPE spawnMaxX = bounds.maxX - margin;
        const FPTYPE spawnMinZ = bounds.minZ + margin;
        const FPTYPE spawnMaxZ = bounds.maxZ - margin;

        const bool spawnOk = (spawnMaxX > spawnMinX) && (spawnMaxZ > spawnMinZ);
        const FPTYPE aMinX = spawnOk ? spawnMinX : bounds.minX;
        const FPTYPE aMaxX = spawnOk ? spawnMaxX : bounds.maxX;
        const FPTYPE aMinZ = spawnOk ? spawnMinZ : bounds.minZ;
        const FPTYPE aMaxZ = spawnOk ? spawnMaxZ : bounds.maxZ;

        const FPTYPE oneMinusEvap = 1.0f - cfg.pEvaporation;

        const uint32_t total = cfg.numDroplets;
        const uint32_t maxTasks = cfg.maxWorkers;
        const uint32_t numBatches = std::min(maxTasks, total);
        const uint32_t batchSize = (total + numBatches - 1u) / numBatches;

        futures.reserve(numBatches);

        const FPTYPE* heights = ws.workHeights.data();
        const FPTYPE* hard = ws.hardness.data();

        for (uint32_t b = 0; b < numBatches; ++b) {
            const uint32_t begin = b * batchSize;
            const uint32_t end = std::min(total, begin + batchSize);

            futures.push_back(tasks.submit([=, &allPts, &tri, &sg, &cfg]() -> std::vector<FPTYPE> {

                std::vector<FPTYPE> localDelta;
                localDelta.assign(allPts.pts.size(), 0.0f);

                for (uint32_t di = begin; di < end; ++di) {

                    uint64_t s = aveng::wyhash64(hydroSeed, uint64_t(di));
                    FPTYPE rx = aveng::u24_to_f01(aveng::wyrand(&s));
                    FPTYPE rz = aveng::u24_to_f01(aveng::wyrand(&s));

                    Droplet d;
                    d.water = cfg.initWater;
                    d.vel = cfg.initVel;
                    d.sediment = 0.0f;
                    d.capacity = cfg.pCapacity;
                    d.alive = true;
                    d.pos = { aMinX + (aMaxX - aMinX) * rx,
                              aMinZ + (aMaxZ - aMinZ) * rz };

                    for (;;) {
                        FPTYPE x = aveng::randSigned(s);
                        FPTYPE y = aveng::randSigned(s);

                        FPTYPE r2 = x * x + y * y;

                        if (r2 > 1e-12f && r2 <= 1.0f) {
                            FPTYPE invLen = 1.0f / std::sqrt(r2);
                            d.dir.x = x * invLen;
                            d.dir.y = y * invLen;
                            break;
                        }
                    }

                    for (uint32_t step = 0; step < cfg.maxSteps; ++step) {
                        if (!d.alive) { break; }

                        auto [t1, ok1] = sg.LocateTriangle(d.pos.x, d.pos.y);
                        if (!ok1) { d.alive = false; break; }

                        aveng::BaryWeights w1{};
                        if (!aveng::Barycentric(allPts, tri, t1, d.pos, w1)) { d.alive = false; break; }

                        FPTYPE h1 = 0.f;
                        if (!aveng::SampleScalar(allPts, tri, t1, d.pos, heights, allPts.pts.size(), h1)) {
                            d.alive = false; break;
                        }

                        FPTYPE dhdx = 0.f, dhdz = 0.f;
                        if (!aveng::TriangleGradient(allPts, tri, t1, heights, allPts.pts.size(), dhdx, dhdz)) {
                            d.alive = false; break;
                        }

                        aveng::Vec2 gradDownhill{ -dhdx, -dhdz };
                        detail::UpdateDirectionUnitStep(d, gradDownhill, cfg);

                        const aveng::Vec2 oldPos = d.pos;
                        d.pos = { d.pos.x + d.dir.x, d.pos.y + d.dir.y };
                        // This is repeated from above.
                        auto [t2, ok2] = sg.LocateTriangle(d.pos.x, d.pos.y);
                        if (!ok2) { d.alive = false; break; }

                        FPTYPE h2 = 0.f;
                        if (!aveng::SampleScalar(allPts, tri, t2, d.pos, heights, allPts.pts.size(), h2)) {
                            d.alive = false; break;
                        }
                        
                        aveng::BaryWeights w2{};
                        if (!aveng::Barycentric(allPts, tri, t2, d.pos, w2)) {
                            d.alive = false; break;
                        }
                        
                        const FPTYPE hdiff = h2 - h1;

                        const auto& T1 = tri.tris[t1];
                        const auto& T2 = tri.tris[t2];
                        const aveng::SiteIndex A1 = T1.A, B1 = T1.B, C1 = T1.C;
                        const aveng::SiteIndex A2 = T2.A, B2 = T2.B, C2 = T2.C;

                        if (hdiff < 0.0f) {
                            const FPTYPE slopeTerm = std::max(-hdiff, cfg.pMinSlope);
                            d.capacity = slopeTerm * d.vel * d.water * cfg.pCapacity;

                            bool excess = false;
                            FPTYPE toDeposit = 0.f;
                            FPTYPE toErode = 0.f;

                            if (d.sediment > d.capacity) {
                                toDeposit = (d.sediment - d.capacity) * cfg.pDeposition;
                                d.sediment -= toDeposit;
                                excess = true;
                            }

                            if (d.capacity > d.sediment) {
                                toErode = (d.capacity - d.sediment) * cfg.pErosion;
                                toErode = std::min(toErode, -hdiff);

                                const FPTYPE avgH = detail::AvgHardnessTri(std::span<const FPTYPE>(hard, allPts.pts.size()),
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
                            if (hdiff > d.sediment) {
                                localDelta[A1] += w1.wa * d.sediment;
                                localDelta[B1] += w1.wb * d.sediment;
                                localDelta[C1] += w1.wc * d.sediment;
                                d.alive = false;
                            }
                            else {
                                const FPTYPE deposit = std::min(d.sediment, hdiff);
                                localDelta[A1] += w1.wa * deposit;
                                localDelta[B1] += w1.wb * deposit;
                                localDelta[C1] += w1.wc * deposit;
                                d.sediment -= deposit;

                                if (d.sediment < 1e-6f) {
                                    d.sediment = 0.f;
                                    d.vel = 0.f;
                                }
                            }
                        }

                        d.vel = std::sqrt(std::max(0.0f, d.vel * d.vel + (-hdiff) * cfg.gravity));

                        d.water *= oneMinusEvap;

                        const FPTYPE slopeMag = std::sqrt(dhdx * dhdx + dhdz * dhdz);
                        if (slopeMag < cfg.flatSlopeEps && d.capacity < cfg.flatCapEps) {
                            d.water *= (1.0f - cfg.flatExtraEvap);
                        }

                        if (d.water < 1e-6f) {
                            d.alive = false;
                            break;
                        }

                        (void)oldPos;
                        (void)A2; (void)B2; (void)C2;
                    }
                }

                return localDelta;
            }));
        }

        return futures;
    }

    void ReduceHydraulicResults(
        ErosionWorkingSet& ws,
        std::vector<std::shared_future<std::vector<float>>>& batchFutures
    ) {
        const size_t N = ws.delta.size();

        for (auto& f : batchFutures) {
            const std::vector<float>& local = f.get();
            for (size_t i = 0; i < N; ++i) {
                ws.delta[i] += local[i];
            }
        }
    }

}
