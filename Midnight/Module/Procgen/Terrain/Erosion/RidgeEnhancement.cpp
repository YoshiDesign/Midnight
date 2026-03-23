#include "RidgeEnhancement.h"
#include <span>
#include <future>
#include <vector>
#include "Module/Procgen/Types.h"
#include "Runtime/Threading/ITaskSystem.h"
#include "Module/Procgen/Terrain/Erosion/Data.h"
#include "Module/Procgen/Terrain/ChunkRecord.h"
#include "Module/Procgen/Noise/Functions.h"

namespace procgen {

    // --------- Helpers you already effectively have via HalfEdge mesh ---------

   // HalfEdge is:
   // struct HalfEdge { SiteIndex origin; int tri; int next; int twin; };

    static inline aveng::SiteIndex EdgeDest(const aveng::Triangulation& tri, aveng::EdgeIndex e) {
        // Destination of edge e is the origin of e.next (standard half-edge convention)
        const auto& he = tri.halfEdges[(size_t)e];
        const auto& heNext = tri.halfEdges[(size_t)he.next];
        return heNext.origin;
    }

    // A small fixed-capacity neighbor list (stack-only, no allocations).
    template<size_t Cap>
    struct SmallNeighbors {
        aveng::SiteIndex v[Cap]{};
        uint32_t n = 0;
        inline void clear() { n = 0; }
        inline void push(aveng::SiteIndex s) { if (n < Cap) v[n++] = s; }
        inline uint32_t size() const { return n; }
        inline aveng::SiteIndex operator[](uint32_t i) const { return v[i]; }
    };

    // Reimplementation of your Go GetAllNeighbors(), but allocation-free.
    // Walk around 'site' using siteEdge[site] as a starting outgoing half-edge.
    // Stops on boundary (twin == -1) or after returning to start.
    static inline void GetAllNeighbors(
        SmallNeighbors<32>& out,
        const aveng::Triangulation& tri,
        aveng::SiteIndex site
    ) {
        out.clear();

        const aveng::EdgeIndex startEdge = (aveng::EdgeIndex)tri.siteEdge[(size_t)site];
        if (startEdge > aveng::kInvalidEdge - 100000) { return; } // Hack! kInvalidEdge is uint, oops!

        aveng::EdgeIndex e = startEdge;

        // Safety cap: should never exceed typical vertex valence.
        // Also prevents infinite loops if topology is unexpectedly broken.
        for (int guard = 0; guard < 64; ++guard) {
            const aveng::SiteIndex dest = EdgeDest(tri, e);
            out.push(dest);

            const int twin = tri.halfEdges[(size_t)e].twin;
            if (twin > aveng::kInvalidEdge - 100000) { break; } // open boundary. Hack! kInvalidEdge is uint, oops!

            e = (aveng::EdgeIndex)tri.halfEdges[(size_t)twin].next;
            if (e == startEdge) { break; }
        }
    }

    static inline float ComputeHeightVariance(
        std::span<const float> heights,
        aveng::SiteIndex site,
        const SmallNeighbors<32>& neighbors
    ) {
        const uint32_t n = neighbors.size();
        if (n == 0) { return 0.0f; }

        const float siteH = heights[(size_t)site];
        double sumSq = 0.0;

        for (uint32_t i = 0; i < n; ++i) {

            const float diff = heights[(size_t)neighbors[i]] - siteH;
            sumSq += double(diff) * double(diff);
        }

        const double mean = sumSq / double(n);
        return (float)std::sqrt(mean);
    }

    static inline float ComputeRidgeness(
        const aveng::Triangulation& tri,
        std::span<const float> heights,
        aveng::SiteIndex site
    ) {
        SmallNeighbors<32> neighbors;
        GetAllNeighbors(neighbors, tri, site);

        const uint32_t n = neighbors.size();
        if (n < 3) { return 0.0f; }

        const float siteH = heights[(size_t)site];
        uint32_t lowerCount = 0;
        uint32_t higherCount = 0;

        for (uint32_t i = 0; i < n; ++i) {
            const float nh = heights[(size_t)neighbors[i]];
            if (nh < siteH) { ++lowerCount; }
            else { ++higherCount; }
        }

        // Pure peaks (all neighbors lower) aren't ridges
        if (higherCount == 0) { return 0.0f; }
        // Pure valleys (all neighbors higher) aren't ridges
        if (lowerCount == 0) { return 0.0f; }

        const float lowerRatio = float(lowerCount) / float(n);

        // More neighbors higher => slope, not ridge
        if (lowerRatio < 0.5f) return 0.0f;

        // Max around ~0.75, linear falloff to 0 at 0.5 or 1.0
        constexpr float optimal = 0.75f;
        const float deviation = std::abs(lowerRatio - optimal);
        float ridgeness = 1.0f - (deviation / 0.25f);
        if (ridgeness < 0.0f) ridgeness = 0.0f;

        const float variance = ComputeHeightVariance(heights, site, neighbors);
        const float varianceFactor = std::min(1.0f, variance / 2.0f);

        return ridgeness * varianceFactor;
    }

    static inline float RidgeNoise2D(float x, float y, uint64_t seed) {
        (void)seed; // shush

        // If your noise is seedless, you can still get seed variance by folding the seed into the coordinate offset. Simple but I need to perf profile
        //const float seedOff = float(ridgeSeed & 0xFFFF) * 0.001f;
        //const float nv = aveng::noise::SimplexNoise2D(nx + seedOff, ny + seedOff);

        // Simplex Noise over points - Deterministic and Fixed
        return aveng::noise::Simplex2D(x, y);

    }

    void ComputeRidgeEnhancement(
        ErosionWorkingSet& ws,
        const aveng::AllPoints& allPts,
        const aveng::Triangulation& tri,
        const aveng::SpatialGrid& /*sg*/,
        const aveng::RidgeParams& cfg,
        uint64_t ridgeSeed,
        aveng::ITaskSystem& tasks
    ) {
        //if (!cfg.Enabled || cfg.Iterations <= 0) return;

        const size_t N = ws.workHeights.size();
        if (N == 0) { return; }

        // ridgeness buffer (reuse ws.delta)
        ws.delta.resize(N);

        // ping-pong destination
        ws.ping.resize(N);

        // Compute min/max ONCE (matches your Go code behavior)
        float minH = ws.workHeights[0];
        float maxH = ws.workHeights[0];
        for (size_t i = 1; i < N; ++i) {
            minH = std::min(minH, ws.workHeights[i]);
            maxH = std::max(maxH, ws.workHeights[i]);
        }
        const float range = maxH - minH;

        const bool absoluteMode = (cfg.MinHeightMode == "absolute");
        const auto meetsMinHeight = [&](float h) -> bool {
            if (absoluteMode) { return h >= cfg.MinHeight; }
            // normalized mode
            if (range < 1e-9f) { return true; }// flat terrain, apply everywhere
            const float t = (h - minH) / range;
            return t >= cfg.MinHeight;
        };

        // Scheduling helper for site-parallel loops
        auto runBatches = [&](uint32_t maxWorkers, auto&& fnPerRange) {
            const uint32_t total = (uint32_t)N;
            const uint32_t numBatches = std::max(1u, std::min(maxWorkers, total));
            const uint32_t batchSize = (total + numBatches - 1u) / numBatches;

            std::vector<std::shared_future<void>> fut;
            fut.reserve(numBatches);

            for (uint32_t b = 0; b < numBatches; ++b) {
                const uint32_t begin = b * batchSize;
                const uint32_t end = std::min(total, begin + batchSize);
                if (begin >= end) break;

                fut.push_back(tasks.submit([=, &fnPerRange]() -> void {
                    fnPerRange(begin, end);
                }));
            }

            for (auto& f : fut) { tasks.wait(f); }
        };

        const uint32_t maxWorkers = (cfg.MaxWorkers == 0 ? 1u : cfg.MaxWorkers);

        for (int iter = 0; iter < cfg.Iterations; ++iter) {
            // Phase 1: compute ridgeness for all sites from CURRENT ws.workHeights
            {
                const float* heights = ws.workHeights.data();
                float* ridg = ws.delta.data();

                runBatches(maxWorkers, [&](uint32_t begin, uint32_t end) {
                    const std::span<const float> hSpan(heights, N);
                    for (uint32_t i = begin; i < end; ++i) {
                        ridg[i] = ComputeRidgeness(tri, hSpan, (aveng::SiteIndex)i);
                    }
                });
            }

            // Phase 2: write output into ws.ping (ping-pong)
            {
                const float* heights = ws.workHeights.data();
                const float* ridg = ws.delta.data();
                float* out = ws.ping.data();

                // Start by copying all heights (so non-ridge points remain unchanged)
                // Parallel copy is fine; memcpy-ish but we’ll keep it simple and deterministic.
                runBatches(maxWorkers, [&](uint32_t begin, uint32_t end) {
                    for (uint32_t i = begin; i < end; ++i) {
                        out[i] = heights[i];
                    }
                });

                // Apply enhancement
                runBatches(maxWorkers, [&](uint32_t begin, uint32_t end) {
                    for (uint32_t i = begin; i < end; ++i) {
                        const float r = ridg[i];
                        if (r < cfg.Threshold) { continue; }

                        const float h = heights[i];
                        if (!meetsMinHeight(h)) { continue; }

                        // Boost proportional to ridgeness
                        const float boost = r * cfg.BoostAmount;

                        // Noise for jaggedness
                        float noise = 0.0f;
                        if (cfg.NoiseAmount != 0.0f && cfg.NoiseFreq != 0.0f) {
                            const aveng::Vec2 p = allPts.pts[i];
                            const float nx = p.x * cfg.NoiseFreq + 1000.0f;
                            const float ny = p.y * cfg.NoiseFreq + 1000.0f;
                            const float nv = RidgeNoise2D(nx, ny, ridgeSeed); // expected ~[-1,1]. SEED IS UNUSED
                            noise = nv * cfg.NoiseAmount;
                        }

                        out[i] = h + (boost + noise);
                    }
                });
            }

            // Swap for next iteration (or final output)
            ws.workHeights.swap(ws.ping);
        }
    }

}