#pragma once
#include <cstdint>
#include <vector>
#include <future>
#include "Runtime/Commands/TerrainCMD.h" // TODO
#include "Module/Procgen/Terrain/Erosion/ErosionManager.h"
#include "Module/Procgen/Types.h"

namespace aveng {

    // Forward declarations to keep compile-times sane.
    struct FinalMeshCPU;
    class ChunkManager;

    /**
     * Game-facing interface for terrain generation / streaming.
     * Owns no terrain data; it just translates game intent into system work.
     */
    class TerrainController {
    public:
        explicit TerrainController(ChunkManager& chunks) noexcept;

        // Called once per tick by the engine (Midnight) so gameplay doesn't need to thread frame indices everywhere.
        void setFrameIndex(uint64_t frameIndex) noexcept;

        /**
         * Beginning from start_coord, generate rows x cols terrain chunks.
         * v1 behavior: request the final CPU mesh for each chunk (schedules work via ChunkManager).
         *
         * Notes:
         * - This does not block/is async. ChunkManager hands you a shared_future.
         * - We keep the futures in an internal list for debugging / inspection.
         */
        void generateChunks(ChunkCoord start_coord, int cols, int rows);

        // Optional: expose what was last requested for debug overlays, etc.
        std::vector<std::shared_future<FinalMeshCPU const*>> const& lastRequestedMeshes() const noexcept;

    private:
        static ChunkCoord offsetCoord(ChunkCoord base, int dx, int dz) noexcept;

        ChunkManager* chunks_ = nullptr; // Primary Manager for Chunk Orchestration - non-owning
        procgen::ErosionManager erosionMgr_;

        uint64_t frameIndex_ = 0;

        // v1: store futures so you can introspect progress in debug UI if you want
        std::vector<std::shared_future<FinalMeshCPU const*>> lastRequested_;
    };
}