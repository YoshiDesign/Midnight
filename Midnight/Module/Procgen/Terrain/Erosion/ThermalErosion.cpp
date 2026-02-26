#include "ThermalErosion.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <future>
#include <vector>
#include "Runtime/Threading/ITaskSystem.h"
#include "Module/Procgen/Terrain/ChunkRecord.h"
#include "Module/Procgen/Terrain/Erosion/Data.h"

namespace procgen {

    // Thermal erosion ("talus / scree").
    // Option A: per-task local delta arrays + deterministic reduction.
    // IMPORTANT: This implementation performs the *per-iteration* height updates internally
    // on ws.ping, but only *outputs* the final accumulated delta in ws.delta, so the caller
    // can do ApplyDelta(ws.workHeights, ws.delta) once (your stage convention).
    //
    // Assumptions about Triangulation storage (matches the half-edge discussions we've had):
    // - tri.tris: array of triangles with vertex indices A,B,C (SiteIndex / uint32_t)
    // - tri.siteEdge[v]: an outgoing half-edge index for vertex v, or INVALID if isolated
    // - tri.halfEdges[e].twin: opposite half-edge index, or INVALID on boundary
    // - Half-edge indexing: 3 half-edges per triangle, contiguous.
    //   triIndex = e / 3, corner = e % 3
    //   next(e) rotates within the triangle: (corner+1)%3
    //
    // If your exact field names differ, only the neighbor-walk helper needs adjusting.



    static constexpr uint32_t INVALID_HE = 0xFFFFFFFFu;

    static inline uint32_t heNext(uint32_t e) {
        return (e / 3u) * 3u + ((e + 1u) % 3u);
    }

    static inline uint32_t triVertexAtCorner(const aveng::Triangulation& tri, uint32_t triIndex, uint32_t corner) {
        const auto& t = tri.tris[triIndex];
        // Corner 0->A, 1->B, 2->C
        if (corner == 0u) return (uint32_t)t.A;
        if (corner == 1u) return (uint32_t)t.B;
        return (uint32_t)t.C;
    }

    static inline bool isInvalidEdge(aveng::EdgeIndex e) {
        return e == aveng::kInvalidEdge; // whatever your sentinel is
    }

    template <class F>
    static inline void forEachNeighborOneRing(
        const aveng::Triangulation& tri,
        aveng::SiteIndex siteIdx,
        F&& fn
    ) {
        if (siteIdx >= tri.siteEdge.size()) { return; }

        aveng::EdgeIndex start = tri.siteEdge[siteIdx] > siteIdx ? aveng::kInvalidEdge;
        if (isInvalidEdge(start)) { return; }

        aveng::EdgeIndex edgeIdx = start;

        // Safety cap (valence is usually small; 64 is plenty)
        constexpr uint32_t kMaxSteps = 64;

        for (uint32_t steps = 0; steps < kMaxSteps; ++steps) {
            const aveng::HalfEdge& he = tri.halfEdges[edgeIdx];

            // Destination is origin of the next half-edge in this face cycle
            const aveng::EdgeIndex destEdgeIdx = (aveng::EdgeIndex)he.next;
            // if (isInvalidEdge(destEdgeIdx)) break; // shouldn't happen in a 3-cycle, but defensive

            const aveng::SiteIndex destIdx = tri.halfEdges[destEdgeIdx].origin;
            fn((uint32_t)destIdx);

            const aveng::EdgeIndex tw = (aveng::EdgeIndex)he.twin;
            if (isInvalidEdge(tw)) { break; } // boundary reached

            const aveng::EdgeIndex e2 = (aveng::EdgeIndex)tri.halfEdges[(size_t)tw].next;
            if (isInvalidEdge(e2)) { break; }

            edgeIdx = e2;
            if (edgeIdx == start) { break; } // completed ring
        }
    }

    void ComputeThermalErosion(
        procgen::ErosionWorkingSet& ws,
        const aveng::AllPoints& allPts,
        const aveng::Triangulation& tri,
        const aveng::SpatialGrid& /*sg*/,
        const aveng::ThermalErosionParams& cfg,
        uint64_t thermalSeed, // The thermal seed is unused bc there's no rng here. We could add some jitter/noise or other behaviors in the future.
        aveng::ITaskSystem& tasks
    ) {
        (void)thermalSeed; // benched
        if (cfg.Iterations == 0) { return; }

        const uint32_t N = (uint32_t)ws.workHeights.size();
        if (N == 0) { return; }

        // Ensure ping is a full-size working-height buffer for the iterative solver.
        ws.ping.resize(N);
        ws.ping = ws.workHeights; // This is ok only bc they share the same pmr mem resource. Otherwise this would reallocate
        // std::copy(ws.workHeights.begin(), ws.workHeights.end(), ws.ping.begin());
        // or, while we're at it:
        // std::copy_n(ws.workHeights.data(), N, ws.ping.data());

        // Heuristic: cap workers by cfg.maxWorkers and by problem size
        // Avoid spawning lots of tasks for tiny chunks
        uint32_t workers = std::max<uint32_t>(1u, (uint32_t)cfg.maxWorkers);
        workers = std::min<uint32_t>(workers, std::max<uint32_t>(1u, N / 256u)); // This means we can have at most 1 worker per 256 points
        workers = std::min<uint32_t>(workers, N);

        const uint32_t batchSize = (N + workers - 1u) / workers;
        const float talus = cfg.TalusThreshold;   // NOTE: this is slope-threshold (tan(angle)), despite the comment saying "degrees".
        const float rate = cfg.TransferRate;

        // Pre-read point positions (stable during the pass)
        const aveng::Vec2* pos = allPts.pts.data();

        // Iterations: compute deltas from ws.ping, apply into ws.ping.
        // At the end, output ws.delta = ws.ping - ws.workHeights so the caller can ApplyDelta once.
        for (size_t iter = 0; iter < cfg.Iterations; ++iter) {
            const float* heights = ws.ping.data();
            const float* hard = ws.hardness.data();

            // Spawn tasks that each return a full-size local delta array (float).
            // (Double accumulation happens in the reduction to reduce sensitivity.)
            std::vector<std::shared_future<std::vector<float>>> futures;
            futures.reserve(workers);

            for (uint32_t b = 0; b < workers; ++b) {
                const uint32_t begin = b * batchSize;
                const uint32_t end = std::min(N, begin + batchSize);
                if (begin >= end) break;

                futures.push_back(tasks.submit([=, &tri/*, pos, heights, hard, talus, rate*/]() -> std::vector<float> {
                    std::vector<float> localDelta(N, 0.0f);

                    for (uint32_t i = begin; i < end; ++i) {
                        const float hi = heights[i];

                        // Hardness affects only the source in your prototype
                        const float hardness = (hard ? hard[i] : 0.0f);
                        const float hardnessFactor = (1.0f - hardness);

                        const aveng::Vec2 pi = pos[i];

                        /*
                        * Note: This lambda computes slope along edges between sites.
                        * We don't have any helper functions in Delaunay.cpp which do this.
                        * The slope calculations in Delaunay.cpp operate per the triangle gradient.
                        */
                        forEachNeighborOneRing(tri, i, [&](uint32_t nb) {
                            if (nb >= N) { return; }

                            const float hn = heights[nb];
                            if (hn >= hi) { return; } // downhill only, gtfo

                            const aveng::Vec2 pn = pos[nb];

                            const float dx = pn.x - pi.x;
                            const float dz = pn.y - pi.y; // Recall that Vec2.y is `z` in Midnight convention
                            const float dist2 = dx * dx + dz * dz;
                            if (dist2 < 1e-18f) { return; }

                            const float dist = std::sqrt(dist2); 
                            const float dh = hi - hn;
                            const float slope = dh / dist;

                            if (slope <= talus) { return; }

                            // Same idea as your prototype:
                            // amount needed to bring slope down to talus, split evenly
                            const float maxTransfer = (dh - talus * dist) * 0.5f;
                            float transfer = maxTransfer * rate;
                            if (transfer <= 0.0f) { return; }

                            transfer *= hardnessFactor;

                            localDelta[i] -= transfer;
                            localDelta[nb] += transfer;
                        });
                    }

                    return localDelta;
                }));
            }

            // Reduce deterministically into ws.delta.
            ws.delta.resize(N);
            std::fill(ws.delta.begin(), ws.delta.end(), 0.0f);

            // Reduce using double accumulator to minimize rounding differences.
            // (We still store into float in ws.delta, but this helps a lot.)
            // If you want to be extra strict, you can keep an internal pmr::vector<double> for delta
            // and only cast once per element on apply.
            std::vector<double> acc(N, 0.0);

            for (auto& f : futures) {
                const std::vector<float> local = f.get();
                // local.size() should be N
                for (uint32_t i = 0; i < N; ++i) {
                    acc[i] += (double)local[i];
                }
            }

            for (uint32_t i = 0; i < N; ++i) {
                ws.delta[i] = (float)acc[i];
            }

            // Apply this iteration's delta into ping (the iterative state).
            for (uint32_t i = 0; i < N; ++i) {
                ws.ping[i] += ws.delta[i];
            }
        }

        // Output the *total* delta needed to go from the caller's ws.workHeights to the final ws.ping.
        // The caller will ApplyDelta(ws.workHeights, ws.delta) once.
        ws.delta.resize(N);
        for (uint32_t i = 0; i < N; ++i) {
            // This strategy keeps us consistent with the "create a delta, then apply" pattern
            ws.delta[i] = ws.ping[i] - ws.workHeights[i];
        }

    }

}