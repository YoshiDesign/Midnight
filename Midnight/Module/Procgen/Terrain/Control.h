#pragma once
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#include "Module/Procgen/Types.h"

namespace procgen {

    // -----------------------------------------------------------------------
    // Readiness helper -- zero-cost poll on a shared_future
    // -----------------------------------------------------------------------

    template<typename T>
    static bool isReady(const std::shared_future<T>& fut) noexcept {
        if (!fut.valid()) return false;
        return fut.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
    }

    // -----------------------------------------------------------------------
    // Stage enumeration
    // -----------------------------------------------------------------------

    enum class TerrainStage : uint8_t {
        Points,
        AllPoints,
        Heights,
        Triangulation,
        SpatialGrid,
        Erosion,
        Mesh,
        Renderable
    };

    inline const char* terrainStageName(TerrainStage s) {
        switch (s) {
            case TerrainStage::Points:        return "Points";
            case TerrainStage::AllPoints:     return "AllPoints";
            case TerrainStage::Heights:       return "Heights";
            case TerrainStage::Triangulation: return "Triangulation";
            case TerrainStage::SpatialGrid:   return "SpatialGrid";
            case TerrainStage::Erosion:       return "Erosion";
            case TerrainStage::Mesh:          return "Mesh";
            case TerrainStage::Renderable:    return "Renderable";
        }
        return "Unknown";
    }

    // -----------------------------------------------------------------------
    // Debug trace events  (compiled out in release via M_DEBUG)
    // -----------------------------------------------------------------------

    struct StageTraceEvent {
        aveng::ChunkCoord coord;
        TerrainStage      stage;
        const char*       action;   // "request", "begin", "defer", "complete"
        std::thread::id   tid;
    };

#ifdef M_DEBUG
    inline void traceStage(aveng::ChunkCoord c, TerrainStage s, const char* action) {
        std::printf("[STAGE] {%d,%d} %s : %s  (tid=%u)\n",
            c.x, c.z,
            terrainStageName(s),
            action,
            static_cast<unsigned>(std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000));
    }
#else
    inline void traceStage(aveng::ChunkCoord, TerrainStage, const char*) {}
#endif

    // -----------------------------------------------------------------------
    // Region admission control
    // Prevents overlapping support-footprint builds from running concurrently.
    // -----------------------------------------------------------------------

    struct ActiveRegion {
        aveng::ChunkCoord center;
        int radius;
    };

    inline bool regionsOverlap(const ActiveRegion& a, const ActiveRegion& b) {
        const bool sepX = (a.center.x + a.radius) < (b.center.x - b.radius)
                       || (b.center.x + b.radius) < (a.center.x - a.radius);
        const bool sepZ = (a.center.z + a.radius) < (b.center.z - b.radius)
                       || (b.center.z + b.radius) < (a.center.z - a.radius);
        return !(sepX || sepZ);
    }

    class TerrainAdmissionController {
    public:
        bool tryAcquire(aveng::ChunkCoord center, int supportRadius) {
            std::lock_guard<std::mutex> lock(mut_);
            ActiveRegion incoming{ center, supportRadius };
            for (const auto& r : active_) {
                if (regionsOverlap(r, incoming))
                    return false;
            }
            active_.push_back(incoming);
            return true;
        }

        void release(aveng::ChunkCoord center, int supportRadius) {
            std::lock_guard<std::mutex> lock(mut_);
            auto it = std::remove_if(active_.begin(), active_.end(),
                [&](const ActiveRegion& r) {
                    return r.center.x == center.x
                        && r.center.z == center.z
                        && r.radius == supportRadius;
                });
            active_.erase(it, active_.end());
        }

    private:
        std::mutex mut_;
        std::vector<ActiveRegion> active_;
    };

} // namespace procgen
