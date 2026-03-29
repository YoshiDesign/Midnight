#pragma once
#include <cstdint>
#include <vector>
#include <future>
#include <memory>
#include "Module/Procgen/Terrain/Erosion/ErosionManager.h"
#include "Module/Procgen/Rendering/BasicTerrainAsset.h"
#include "Module/Procgen/Terrain/Control.h"
#include "Module/Procgen/Rendering/VkTerrain.h"
#include "Module/Procgen/Rendering/TerrainResourcePool.h"
#include "Module/Procgen/Types.h"
#include "Utils/Timer.h"

// TODO - We're going to make a TerrainRenderSystem
#include <vulkan/vulkan_core.h>

namespace procgen {
    struct TerrainRenderable;
}

/**
 * Notes about the new resource pool:
 * The pool is not pre-populated -- it's a recycling pool that starts empty and fills up organically as chunks complete their lifecycle. Here's the flow:

Cold start (pool empty): The very first wave of chunks will always hit the "fall back to init" path because there's nothing to recycle yet. This is expected -- the first N chunks pay the full vmaCreateBuffer cost just like they do today.

Steady state (pool warm): Once the player starts moving and chunks cycle through eviction, buffers flow into the pool from two sources:

retireCompletedUploads -- when a GPU upload fence signals, the CPU renderable (with its ~2MB of vectors) gets its vectors cleared and the empty shell is pushed into pool_.renderables. This avoids the cost of deallocating and reallocating the TerrainRenderable struct itself.

flushDeferredDeletes -- when an evicted chunk's deferred frame count elapses (after kDeferFrames), its VBO, IBO, input SSBO, and output SSBO structs get moved into pool_.vbo, pool_.ibo, etc. instead of being destroyed. The Vulkan handles survive -- device buffer, staging buffer, VMA allocations -- all still valid.

Then when prepareChunkUpload runs for the next incoming chunk, it checks the pool first. If there's a buffer whose bufferSize >= needed, it grabs it. The existing Vulkan allocation is reused: fillStaging just maps the same staging buffer and overwrites its contents, recordCopy copies to the same device buffer. Zero vmaCreateBuffer / vmaDestroyBuffer calls on the hot path.

So the lifecycle looks like:
 */

namespace aveng {

    /*
    * The TerrainController is a scheduler, in charge of:
    * - What work is allowed to begin
    * - What work is currently in flight
    * - When completed work becomes usable
    * - When old GPU resources are actually safe to destroy
    * 
    * The recurring engine ideas are:
    * - work admission       [Requested, Deferred]
    * - in-flight tracking   [Building]
    * - async completion     [CpuReady] 
    * - residency state      [Resident]
    * - deferred destruction [Retired, Destroyed]
    * - budget enforcement
    * 
    * This is just the first place I'm utilizing this philosophy
    * 
    * This results in:
    * - frame-budgeted work
    * - asynchronous resource lifetime management
    * - admission / backpressure control
    * - GPU pipeline latency awareness
    * 
    * Implemented Principles:
    * ## `deferredCleanups_` - frame-latency-aware cleanup. 
    * Destruction is also work, and on the GPU it has timing constraints. 
    * We do not want to use vkQueueWaitIdle to clean up evicted chunks.
    * (i.e.) If a chunk was recently used by commands the GPU may still be executing, then destroying 
    * its buffers immediately is dangerous unless you force a hard sync like vkQueueWaitIdle.
    * 
    * TODO: Document the rest
    */

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
        void retireCompletedUploads();
        void buildAndSubmitUploadBatch();
        void flushDeferredDeletes();

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

        Timer vkBufferInitTimer{};
        Timer vkCleanupOneTimer{};
        Timer vkCleanupDeferredDeletesTimer{};
        Timer vkCopyBufferTimer{};
        Timer retireTimer{};
        Timer drainTimer{};

        // Dummy
        uint64_t frameIndex_ = 0;


        // std::unique_ptr<procgen::TerrainRenderable> lastRequested_;

        // CEO (Chunk Executive Officer)
        ChunkManager* chunks_ = nullptr; // Primary Manager for Chunk Orchestration - non-owning
        static constexpr int kMaxChunksPerUploadBatch = 3;
        EngineDevice& engineDevice_;
        VkRenderData& renderData_;
        procgen::ErosionManager erosionMgr_;

        // Region admission control: prevents overlapping 5x5 support footprints
        // from being built concurrently (the primary fix for dependency starvation).
        static constexpr int kSupportRadius = 2; // 5x5 neighborhood
        procgen::TerrainAdmissionController admission_;
        std::vector<ChunkCoord> deferredRequests_;

        // Non-blocking batched GPU upload infrastructure
        TerrainUploadBatch uploadBatch_;

        // Deferred GPU resource destruction (avoids vkQueueWaitIdle during eviction)
        std::vector<DeferredGpuCleanup> deferredCleanups_;
        static constexpr uint64_t kDeferFrames = 3;

        // Recycling pool for Vulkan buffers and CPU renderables
        TerrainResourcePool pool_;

    };
}