#pragma once
#include <cstdint>
#include <vector>
#include <future>
#include <memory>
#include "Module/Procgen/Terrain/Erosion/ErosionManager.h"
#include "Module/Procgen/Rendering/BasicTerrainAsset.h"
#include "Module/Procgen/Terrain/Control.h"
#include "Module/Procgen/Types.h"

// TODO - We're going to make a TerrainRenderSystem
#include <vulkan/vulkan_core.h>

namespace procgen {
    struct TerrainRenderable;
}

namespace aveng {

    // Forward declarations to keep compile-times sane.
    struct FinalMeshCPU;
    struct VkRenderData;
    class ChunkManager;
    class EngineDevice;

    enum class TerrainStreamMode {
        LinearFlight,
        AllRange
    };

    struct LinearFlightState {
        int maxCenterWaveRequested = -1;
        int maxFanWaveRequested = -1;
        int baseX = 0;
        int baseZ = 0;
    };

    struct StreamUpdateContext {
        ChunkCoord playerChunk;
        uint64_t frameIndex;
    };

    struct StreamCommandBuffer {
        std::vector<ChunkCoord> requestCenters;
        std::vector<ChunkCoord> evictCenters;
    };

    struct LinearStreamPolicy {
        int lateralRadius = 1;
        int forwardRows = 9;
        int backwardRows = 1;
        int evictRadiusX = 6;
        // int evictRadiusZ = 7;
        int evictBackwardZ = 2;
    };

    struct AllRangeStreamPolicy {
        int radius = 2;     // produces NxN around arena center
        bool keepFullGridResident = true;
    };

    struct TerrainStreamPolicy {
        TerrainStreamMode mode = TerrainStreamMode::LinearFlight;
        LinearStreamPolicy linear;
        AllRangeStreamPolicy allRange;
    };

    struct StreamedChunkState {
        enum class Status {
            Requested,
            Resident,
            Evicting
        };
        Status status;
    };

    /**
     * Game-facing interface for terrain generation / streaming.
     * Owns no terrain data; it just translates game intent into system work.
     */
    class TerrainController {
    private:
        static ChunkCoord offsetCoord(ChunkCoord base, int dx, int dz) noexcept;

    public:
        explicit TerrainController(EngineDevice& engineDevice, VkRenderData& renderData, ChunkManager& chunks) noexcept;

        ~TerrainController();

        // Called once per tick by the engine (Midnight) so gameplay doesn't need to thread frame indices everywhere.
        void setFrameIndex(uint64_t frameIndex) noexcept;

        // Forward requested params to the ChunkManager
        void setTerrainConfig(TerrainConfig tcfg);
        // Forward requested params to the ChunkManager
        void setTerrainNoiseParams(noise::NoiseParams noise);
        // Forward requested params to the ChunkManager
        void setTerrainWeatheringParams(ErosionSettings erosion);

        // VK Data - Compute Shader Terrain Rendering Settings
        void setTerrainSettingsUbo(VkBuffer buffer, VkDeviceSize size) noexcept {
            settingsUboBuffer_ = buffer;
            settingsUboSize_ = size;
        }

        /* Getter */
        float getChunkSize();

        /* Streaming Policy */

        /* Operational Requirements */
        void serviceCpuReadyChunks();

        void update(/*const Camera& camera*/);

        void render(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex);
        void renderDebug(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex);

        void cleanup();

        void cleanupOne(procgen::TerrainChunkSlot& slot);
        /**
         * Request the final renderable data for compute and graphics.
         * The renderable comprises a 3x3 region of space with up to a 5x5 region
         * in order to support computations (vertex normals, weights, adjacency data)
         *
         * Notes:
         * - This does not block/is async. ChunkManager hands you a shared_future.
         * - We keep the futures in an internal list for debugging / inspection.
         */
        void generateChunks(ChunkCoord start_coord);

        void evictChunk(ChunkCoord center);

        // void collectRenderable();

        void ensureChunkRequested(ChunkCoord coord);

        void drainCompletedTerrain();

        bool hasAllPointsReady(const ChunkCoord coord) noexcept ;
        bool hasRegionReady(ChunkCoord center) noexcept;
        bool hasRegionComplete(ChunkCoord center) noexcept;

        // Expose what was last requested for debug overlays, etc.
        // std::unique_ptr<procgen::TerrainRenderable> const& lastRequestedRenderable() const noexcept;

    private:

        /* ChunkCoord represents the center of a 3x3 renderable region,
         * though we actually receive data for a 5x5 region to support compute.
         * Compute dispatch only needs to occur once the renderable is completed
         */
        std::unordered_map<ChunkCoord, procgen::TerrainChunkSlot, ChunkCoordHash> slots_;

        std::unordered_map<ChunkCoord, StreamedChunkState, ChunkCoordHash> managed_;
        ChunkCoord currentCenter_;
        TerrainStreamMode currentMode_;

        TerrainStreamPolicy policy_{};

        VkBasicTerrainPushConstant pc;
        VkBasicTerrainDebugPC dpc;

        // Shared terrain compute settings UBO (owned by Renderer)
        VkBuffer settingsUboBuffer_ = VK_NULL_HANDLE;
        VkDeviceSize settingsUboSize_ = 0;

       

        // Dummy
        uint64_t frameIndex_ = 0;


        // std::unique_ptr<procgen::TerrainRenderable> lastRequested_;

        // CEO (Chunk Executive Officer)
        ChunkManager* chunks_ = nullptr; // Primary Manager for Chunk Orchestration - non-owning
        const int kMaxUploadsPerFrame = 2;
        EngineDevice& engineDevice_;
        VkRenderData& renderData_;
        procgen::ErosionManager erosionMgr_;

        // Region admission control: prevents overlapping 5x5 support footprints
        // from being built concurrently (the primary fix for dependency starvation).
        static constexpr int kSupportRadius = 2; // 5x5 neighborhood
        procgen::TerrainAdmissionController admission_;
        std::vector<ChunkCoord> deferredRequests_;

    };
}