#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan_core.h>
#include "Module/Procgen/Terrain/Erosion/ErosionManager.h"
#include "Module/Procgen/Rendering/BasicTerrainAsset.h"
#include "Module/Procgen/Terrain/Control.h"
#include "Module/Procgen/Rendering/VkTerrain.h"
#include "Module/Procgen/Rendering/TerrainResourcePool.h"
#include "Module/Procgen/Terrain/TerrainPool.h"
#include "Module/Procgen/Types.h"
#include "Utils/Timer.h"


namespace procgen {
    class ChunkManager2;
}

namespace aveng {

    // Forward declarations to keep compile-times sane.
    struct FinalMeshCPU;
    struct VkRenderData;
    
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
        bool initialized = false;
    };

    struct StreamUpdateContext {
        procgen::ChunkCoord playerChunk;
        uint64_t frameIndex;
    };

    struct StreamCommandBuffer {
        std::vector<procgen::ChunkCoord> requestCenters;
        std::vector<procgen::ChunkCoord> evictCenters;
    };

    struct LinearStreamPolicy {
        int lateralRadius{ 1 };
        int forwardRows{ 6 };
        int backwardRows { 1 };
        int evictRadiusX { 7 };
        // int evictRadiusZ {7};
        int evictBackwardZ { 2 };
        int kMaxRequests { 2 }; // 2 requests max per frame
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

    public:
        explicit TerrainController(EngineDevice& engineDevice, VkRenderData& renderData, procgen::ChunkManager2& chunks) noexcept;

        ~TerrainController();

        void init();

        // Called once per tick by the engine (Midnight) so gameplay doesn't need to thread frame indices everywhere.
        void setFrameIndex(uint64_t frameIndex) noexcept;

        // Forward requested params to the ChunkManager
        void setTerrainConfig(procgen::TerrainConfig tcfg);
        // Forward requested params to the ChunkManager
        void setTerrainNoiseParams(noise::NoiseParams noise);
        // Forward requested params to the ChunkManager
        void setTerrainWeatheringParams(procgen::ErosionSettings erosion);

        // VK Data - Compute Shader Terrain Rendering Settings
        void setTerrainSettingsUbo(VkBuffer buffer, VkDeviceSize size) noexcept {
            settingsUboBuffer_ = buffer;
            settingsUboSize_ = size;
        }

        /* Getter */
        float getChunkSize();

        void setDrawCenter(glm::vec3 worldPos) noexcept;
        const procgen::ChunkCoord getDrawCenter() { return currentCenter_; };

        /* Streaming Policy */

        /* Operational Requirements */
        void serviceCpuReadyChunks();
        void retireCompletedUploads();
        void buildAndSubmitUploadBatch();
        void flushDeferredDeletes();

        void tick();

        void render(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex);
        void renderDebug(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex);

        void cleanup();

        void cleanupOne(); // TODO
        /**
         * Request the final renderable data for compute and graphics.
         * The renderable comprises a 3x3 region of space with up to a 5x5 region
         * in order to support computations (vertex normals, weights, adjacency data)
         *
         * Notes:
         * - This does not block/is async. ChunkManager enqueues work on the thread pool.
         * - Completion is signaled via the ConcurrentQueue (CompletionNotice).
         */
        void generateChunks(procgen::ChunkCoord start_coord);

        void evictChunk(procgen::ChunkCoord center);

        void drainCompletedTerrain();

        bool hasSpatialGridReady(const procgen::ChunkCoord coord) noexcept ;
        bool hasRegionReady(procgen::ChunkCoord center) noexcept;
        bool hasRegionComplete(procgen::ChunkCoord center) noexcept;

        // Diagnostics for frame pacing
        //int countActiveUploads() const;
        //int countCpuReadySlots() const;
        //int countResidentSlots() const;

        // Expose what was last requested for debug overlays, etc.
        // std::unique_ptr<procgen::TerrainRenderable> const& lastRequestedRenderable() const noexcept;

    private:

        uint32_t allocateSlot(procgen::ChunkCoord coord);
        void releaseSlot(uint32_t idx);

        /* Contiguous slot storage with index-based access.
         * coordToSlot_ resolves coord-based lookups (eviction, etc).
         * freeSlots_ recycles indices so the vector never grows during gameplay.
         * Exactly how our model instances work.
         */

        std::unordered_map<procgen::ChunkCoord, procgen::ChunkHandle, procgen::ChunkCoordHash> coordToSlot_;
        //std::vector<uint32_t> freeSlots_;

        // Region admission control: prevents overlapping 5x5 support footprints
        // from being built concurrently (the primary fix for dependency starvation).
        procgen::TerrainAdmissionController admission_;
        std::vector<procgen::ChunkCoord> deferredRequests_;

        // Non-blocking batched GPU upload infrastructure (Buffered: one per frame-in-flight)
        std::array<TerrainUploadBatch, 3> uploadBatches_;

        // Deferred GPU resource destruction (avoids vkQueueWaitIdle during eviction)
        std::vector<DeferredGpuCleanup> deferredCleanups_;

        // Recycling pool for Vulkan buffers and CPU renderables
        TerrainResourcePool pool_;
        procgen::TerrainPool terrain_pool;


        std::unordered_map<procgen::ChunkCoord, StreamedChunkState, procgen::ChunkCoordHash> managed_;
        procgen::ChunkCoord currentCenter_;

        TerrainStreamMode currentMode_;
        TerrainStreamPolicy policy_{};

        VkBasicTerrainPushConstant pc;
        VkBasicTerrainDebugPC dpc;

        // Shared terrain compute settings UBO (owned by Renderer!)
        VkBuffer settingsUboBuffer_ = VK_NULL_HANDLE;
        VkDeviceSize settingsUboSize_ = 0;

        Timer vkBufferInitTimer{};
        Timer vkCleanupOneTimer{};
        Timer vkCleanupDeferredDeletesTimer{};
        Timer vkCopyBufferTimer{};
        Timer retireTimer{};
        Timer drainTimer{};
        Timer tickPhaseTimer_{};
        Timer evictionTimer{};

        // Dummy
        uint64_t frameIndex_ = 0;

        static constexpr uint64_t kDeferFrames = 3;
        static constexpr int kMaxChunksPerUploadBatch = 3;
        static constexpr int kDrawRadius = 4;    // Chunk space
        static constexpr int kSupportRadius = 2; // 5x5 neighborhood
        static constexpr uint32_t kMinSlotReserve = 50;

        // std::unique_ptr<procgen::TerrainRenderable> lastRequested_;

        // CEO (Chunk Executive Officer)
        procgen::ChunkManager2* chunks_ = nullptr; // Primary Manager for Chunk Orchestration - non-owning
        EngineDevice& engineDevice_;
        VkRenderData& renderData_;
        procgen::ErosionManager erosionMgr_;

    };

    struct TerrainRequest {
        procgen::TerrainPool* pool = nullptr;
        TerrainController* controller = nullptr;

        ChunkCoord center{};
        uint32_t requestId = 0;
        uint32_t batchesPerChunk = 0;

        uint16_t slots[49]{};
        ChunkCoord coords[49]{};

        alignas(64) std::atomic<uint32_t> pointsRemaining{ 0 };
        alignas(64) std::atomic<uint32_t> terminalRemaining{ 0 };
        alignas(64) std::atomic<uint32_t> erosionPerChunk[9]{};

        void init(
            TerrainController* owner,
            procgen::TerrainPool* p,
            ChunkCoord c,
            uint32_t id,
            uint32_t batches
        ) {
            controller = owner;
            pool = p;
            center = c;
            requestId = id;
            batchesPerChunk = batches;

            pointsRemaining.store(49, std::memory_order_relaxed);
            terminalRemaining.store(25, std::memory_order_relaxed);

            for (auto& e : erosionPerChunk) {
                e.store(0, std::memory_order_relaxed);
            }

            // Fill slots[] and coords[] separately during admission.
        }
    };
}