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

    // --------- Helpers (file-local, unchanged) ---------

    static inline aveng::SiteIndex EdgeDest(const aveng::Triangulation& tri, aveng::EdgeIndex e) {
        const auto& he = tri.halfEdges[(size_t)e];
        const auto& heNext = tri.halfEdges[(size_t)he.next];
        return heNext.origin;
    }

    template<size_t Cap>
    struct SmallNeighbors {
        aveng::SiteIndex v[Cap]{};
        uint32_t n = 0;
        inline void clear() { n = 0; }
        inline void push(aveng::SiteIndex s) { if (n < Cap) v[n++] = s; }
        inline uint32_t size() const { return n; }
        inline aveng::SiteIndex operator[](uint32_t i) const { return v[i]; }
    };

    static inline void GetAllNeighbors(
        SmallNeighbors<32>& out,
        const aveng::Triangulation& tri,
        aveng::SiteIndex site
    ) {
        out.clear();

        const aveng::EdgeIndex startEdge = (aveng::EdgeIndex)tri.siteEdge[(size_t)site];
        if (startEdge > aveng::kInvalidEdge - 100000) { return; }

        aveng::EdgeIndex e = startEdge;

        for (int guard = 0; guard < 64; ++guard) {
            const aveng::SiteIndex dest = EdgeDest(tri, e);
            out.push(dest);

            const int twin = tri.halfEdges[(size_t)e].twin;
            if (twin > aveng::kInvalidEdge - 100000) { break; }

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

        if (higherCount == 0) { return 0.0f; }
        if (lowerCount == 0) { return 0.0f; }

        const float lowerRatio = float(lowerCount) / float(n);

        if (lowerRatio < 0.5f) return 0.0f;

        constexpr float optimal = 0.75f;
        const float deviation = std::abs(lowerRatio - optimal);
        float ridgeness = 1.0f - (deviation / 0.25f);
        if (ridgeness < 0.0f) ridgeness = 0.0f;

        const float variance = ComputeHeightVariance(heights, site, neighbors);
        const float varianceFactor = std::min(1.0f, variance / 2.0f);

        return ridgeness * varianceFactor;
    }

    static inline float RidgeNoise2D(float x, float y, uint64_t seed) {
        (void)seed;
        return aveng::noise::Simplex2D(x, y);
    }

    // --------- Batch submission helpers ---------

    static void submitVoidBatches(
        std::vector<std::shared_future<void>>& out,
        uint32_t total,
        uint32_t maxWorkers,
        aveng::ITaskSystem& tasks,
        auto&& fnPerRange
    ) {
        const uint32_t numBatches = std::max(1u, std::min(maxWorkers, total));
        const uint32_t batchSize  = (total + numBatches - 1u) / numBatches;

        out.reserve(numBatches);

        for (uint32_t b = 0; b < numBatches; ++b) {
            const uint32_t begin = b * batchSize;
            const uint32_t end   = std::min(total, begin + batchSize);
            if (begin >= end) break;

            out.push_back(tasks.submit([=]() -> void {
                fnPerRange(begin, end);
            }));
        }
    }

    // -----------------------------------------------------------------------
    // Public API
    // -----------------------------------------------------------------------

    void InitRidgeEnhancement(
        ErosionWorkingSet& ws,
        const aveng::RidgeParams& cfg,
        float& outMinH,
        float& outMaxH,
        uint32_t& outMaxWorkers
    ) {
        const size_t N = ws.workHeights.size();

        ws.delta.resize(N);
        ws.ping.resize(N);

        float minH = ws.workHeights[0];
        float maxH = ws.workHeights[0];
        for (size_t i = 1; i < N; ++i) {
            minH = std::min(minH, ws.workHeights[i]);
            maxH = std::max(maxH, ws.workHeights[i]);
        }

        outMinH = minH;
        outMaxH = maxH;
        outMaxWorkers = (cfg.MaxWorkers == 0 ? 1u : cfg.MaxWorkers);
    }

    std::vector<std::shared_future<void>> SubmitRidgenessCompute(
        ErosionWorkingSet& ws,
        const aveng::Triangulation& tri,
        uint32_t maxWorkers,
        aveng::ITaskSystem& tasks
    ) {
        const size_t N = ws.workHeights.size();
        const float* heights = ws.workHeights.data();
        float* ridg = ws.delta.data();

        std::vector<std::shared_future<void>> futures;
        submitVoidBatches(futures, (uint32_t)N, maxWorkers, tasks,
            [=, &tri](uint32_t begin, uint32_t end) {
                const std::span<const float> hSpan(heights, N);
                for (uint32_t i = begin; i < end; ++i) {
                    ridg[i] = ComputeRidgeness(tri, hSpan, (aveng::SiteIndex)i);
                }
            });
        return futures;
    }

    std::vector<std::shared_future<void>> SubmitRidgeCopy(
        ErosionWorkingSet& ws,
        uint32_t maxWorkers,
        aveng::ITaskSystem& tasks
    ) {
        const size_t N = ws.workHeights.size();
        const float* heights = ws.workHeights.data();
        float* out = ws.ping.data();

        std::vector<std::shared_future<void>> futures;
        submitVoidBatches(futures, (uint32_t)N, maxWorkers, tasks,
            [=](uint32_t begin, uint32_t end) {
                for (uint32_t i = begin; i < end; ++i) {
                    out[i] = heights[i];
                }
            });
        return futures;
    }

    std::vector<std::shared_future<void>> SubmitRidgeApply(
        ErosionWorkingSet& ws,
        const aveng::AllPoints& allPts,
        const aveng::RidgeParams& cfg,
        uint64_t ridgeSeed,
        float minH,
        float maxH,
        uint32_t maxWorkers,
        aveng::ITaskSystem& tasks
    ) {
        const size_t N = ws.workHeights.size();
        const float* heights = ws.workHeights.data();
        const float* ridg    = ws.delta.data();
        float*       out     = ws.ping.data();

        const float range = maxH - minH;
        const bool absoluteMode = (cfg.MinHeightMode == "absolute");

        // Capture cfg fields by value for the lambda
        const float cfgMinHeight   = cfg.MinHeight;
        const float cfgThreshold   = cfg.Threshold;
        const float cfgBoostAmount = cfg.BoostAmount;
        const float cfgNoiseAmount = cfg.NoiseAmount;
        const float cfgNoiseFreq   = cfg.NoiseFreq;

        std::vector<std::shared_future<void>> futures;
        submitVoidBatches(futures, (uint32_t)N, maxWorkers, tasks,
            [=, &allPts](uint32_t begin, uint32_t end) {
                auto meetsMinHeight = [&](float h) -> bool {
                    if (absoluteMode) { return h >= cfgMinHeight; }
                    if (range < 1e-9f) { return true; }
                    const float t = (h - minH) / range;
                    return t >= cfgMinHeight;
                };

                for (uint32_t i = begin; i < end; ++i) {
                    const float r = ridg[i];
                    if (r < cfgThreshold) { continue; }

                    const float h = heights[i];
                    if (!meetsMinHeight(h)) { continue; }

                    const float boost = r * cfgBoostAmount;

                    float noise = 0.0f;
                    if (cfgNoiseAmount != 0.0f && cfgNoiseFreq != 0.0f) {
                        const aveng::Vec2 p = allPts.pts[i];
                        const float nx = p.x * cfgNoiseFreq + 1000.0f;
                        const float ny = p.y * cfgNoiseFreq + 1000.0f;
                        const float nv = RidgeNoise2D(nx, ny, ridgeSeed);
                        noise = nv * cfgNoiseAmount;
                    }

                    out[i] = h + (boost + noise);
                }
            });
        return futures;
    }

    void SwapRidgeIteration(ErosionWorkingSet& ws) {
        ws.workHeights.swap(ws.ping);
    }

}
