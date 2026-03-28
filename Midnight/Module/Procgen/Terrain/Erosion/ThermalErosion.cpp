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
    // Assumptions about Triangulation storage (matches the half-edge discussions we've had):
    // - tri.tris: array of triangles with vertex indices A,B,C (SiteIndex / uint32_t)
    // - tri.siteEdge[v]: an outgoing half-edge index for vertex v, or INVALID if isolated
    // - tri.halfEdges[e].twin: opposite half-edge index, or INVALID on boundary
    // - Half-edge indexing: 3 half-edges per triangle, contiguous.
    //   triIndex = e / 3, corner = e % 3
    //   next(e) rotates within the triangle: (corner+1)%3

    static constexpr uint32_t INVALID_HE = 0xFFFFFFFFu;

    static inline uint32_t heNext(uint32_t e) {
        return (e / 3u) * 3u + ((e + 1u) % 3u);
    }

    static inline uint32_t triVertexAtCorner(const aveng::Triangulation& tri, uint32_t triIndex, uint32_t corner) {
        const auto& t = tri.tris[triIndex];
        if (corner == 0u) return (uint32_t)t.A;
        if (corner == 1u) return (uint32_t)t.B;
        return (uint32_t)t.C;
    }

    static inline bool isInvalidEdge(aveng::EdgeIndex e) {
        return e == aveng::kInvalidEdge;
    }

    template <class F>
    static inline void forEachNeighborOneRing(
        const aveng::Triangulation& tri,
        aveng::SiteIndex siteIdx,
        F&& fn
    ) {
        if (siteIdx >= tri.siteEdge.size()) { return; }

        aveng::EdgeIndex start = tri.siteEdge[siteIdx] > siteIdx ? tri.siteEdge[siteIdx] : aveng::kInvalidEdge;
        if (isInvalidEdge(start)) { return; }

        aveng::EdgeIndex edgeIdx = start;

        constexpr uint32_t kMaxSteps = 64;

        for (uint32_t steps = 0; steps < kMaxSteps; ++steps) {
            const aveng::HalfEdge& he = tri.halfEdges[edgeIdx];

            const aveng::EdgeIndex destEdgeIdx = (aveng::EdgeIndex)he.next;

            const aveng::SiteIndex destIdx = tri.halfEdges[destEdgeIdx].origin;
            fn((uint32_t)destIdx);

            const aveng::EdgeIndex tw = (aveng::EdgeIndex)he.twin;
            if (isInvalidEdge(tw)) { break; }

            const aveng::EdgeIndex e2 = (aveng::EdgeIndex)tri.halfEdges[(size_t)tw].next;
            if (isInvalidEdge(e2)) { break; }

            edgeIdx = e2;
            if (edgeIdx == start) { break; }
        }
    }

    // -----------------------------------------------------------------------

    void InitThermalErosion(
        ErosionWorkingSet& ws,
        const aveng::ThermalErosionParams& cfg,
        uint32_t& outWorkers,
        uint32_t& outBatchSize
    ) {
        const uint32_t N = (uint32_t)ws.workHeights.size();

        ws.ping.resize(N);
        ws.ping = ws.workHeights;

        uint32_t workers = std::max<uint32_t>(1u, (uint32_t)cfg.maxWorkers);
        workers = std::min<uint32_t>(workers, std::max<uint32_t>(1u, N / 256u));
        workers = std::min<uint32_t>(workers, N);

        outWorkers  = workers;
        outBatchSize = (N + workers - 1u) / workers;
    }

    std::vector<std::shared_future<std::vector<float>>> SubmitThermalIterationBatches(
        ErosionWorkingSet& ws,
        const aveng::AllPoints& allPts,
        const aveng::Triangulation& tri,
        const aveng::ThermalErosionParams& cfg,
        uint32_t workers,
        uint32_t batchSize,
        aveng::ITaskSystem& tasks
    ) {
        const uint32_t N = (uint32_t)ws.workHeights.size();
        const float talus = cfg.TalusThreshold;
        const float rate  = cfg.TransferRate;
        const aveng::Vec2* pos = allPts.pts.data();
        const float* heights = ws.ping.data();
        const float* hard    = ws.hardness.data();

        std::vector<std::shared_future<std::vector<float>>> futures;
        futures.reserve(workers);

        for (uint32_t b = 0; b < workers; ++b) {
            const uint32_t begin = b * batchSize;
            const uint32_t end   = std::min(N, begin + batchSize);
            if (begin >= end) break;

            futures.push_back(tasks.submit([=, &tri]() -> std::vector<float> {
                std::vector<float> localDelta(N, 0.0f);

                for (uint32_t i = begin; i < end; ++i) {
                    const float hi = heights[i];

                    const float hardness = (hard ? hard[i] : 0.0f);
                    const float hardnessFactor = (1.0f - hardness);

                    const aveng::Vec2 pi = pos[i];

                    forEachNeighborOneRing(tri, i, [&](uint32_t nb) {
                        if (nb >= N) { return; }

                        const float hn = heights[nb];
                        if (hn >= hi) { return; }

                        const aveng::Vec2 pn = pos[nb];

                        const float dx = pn.x - pi.x;
                        const float dz = pn.y - pi.y;
                        const float dist2 = dx * dx + dz * dz;
                        if (dist2 < 1e-18f) { return; }

                        const float dist = std::sqrt(dist2); 
                        const float dh = hi - hn;
                        const float slope = dh / dist;

                        if (slope <= talus) { return; }

                        const float maxTransfer = (dh - talus * dist) * 0.5f;
                        float transfer = maxTransfer * rate;
                        if (transfer <= 0.0f) { return; }

                        transfer *= hardnessFactor;

                        localDelta[i]  -= transfer;
                        localDelta[nb] += transfer;
                    });
                }

                return localDelta;
            }));
        }

        return futures;
    }

    void ReduceThermalIteration(
        ErosionWorkingSet& ws,
        std::vector<std::shared_future<std::vector<float>>>& batchFutures
    ) {
        const uint32_t N = (uint32_t)ws.workHeights.size();

        ws.delta.resize(N);
        std::fill(ws.delta.begin(), ws.delta.end(), 0.0f);

        std::vector<double> acc(N, 0.0);

        for (auto& f : batchFutures) {
            const std::vector<float>& local = f.get();
            for (uint32_t i = 0; i < N; ++i) {
                acc[i] += (double)local[i];
            }
        }

        for (uint32_t i = 0; i < N; ++i) {
            ws.delta[i] = (float)acc[i];
        }

        for (uint32_t i = 0; i < N; ++i) {
            ws.ping[i] += ws.delta[i];
        }
    }

    void FinalizeThermalDelta(ErosionWorkingSet& ws) {
        const uint32_t N = (uint32_t)ws.workHeights.size();
        ws.delta.resize(N);
        for (uint32_t i = 0; i < N; ++i) {
            ws.delta[i] = ws.ping[i] - ws.workHeights[i];
        }
    }

}
