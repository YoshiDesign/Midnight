#pragma once
#include <atomic>
#include <vector>
#include "Module/Procgen/Types2.h"

namespace procgen {

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
        ChunkCoord center;
        int radius;
    };

    inline bool regionsOverlap(const ActiveRegion& a, const ActiveRegion& b) {
        const bool sepX = (a.center.x + a.radius) < (b.center.x - b.radius)
                       || (b.center.x + b.radius) < (a.center.x - a.radius);
        const bool sepZ = (a.center.z + a.radius) < (b.center.z - b.radius)
                       || (b.center.z + b.radius) < (a.center.z - a.radius);
        return !(sepX || sepZ);
    }

    /*
    * Admission Control - Correctness and backpressure for the TerrainController
    * The purpose of this is to prevent the terrain system from becoming self destructive under load.
    * 
    * Current Policy Detail:
    * TODO
    */
    class TerrainAdmissionController {
    public:

        /*
        * Note: I'm leaving the atomic counter in here for now, but if we design
        * a system in which the TerrainAdmissionController is only ever operated
        * on by the main OS thread, then we don't need atomics at all
        * 
        * This behaves very much like Go's WaitGroup. There's a lot of room 
        * for additional features but for now it's just the bare minimum.
        */

        bool allow() {
            if (activeCount_.load(std::memory_order_relaxed) > 2) { return false; }
            activeCount_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        bool release() {
#ifdef M_DEBUG
            aveng::Logger::log(1, "[TerrainAdmissionController] - RELEASE\n");
#endif
            activeCount_.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }

    private:
		std::atomic<uint32_t> activeCount_{ 0 };
    };

} // namespace procgen
