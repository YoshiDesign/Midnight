#pragma once
#include <cstdint>
#include <vector>
#include <future>
#include <memory>
// #include "Runtime/Commands/TerrainCMD.h" // TODO. Maybe...
#include "Module/Procgen/Terrain/Erosion/ErosionManager.h"
#include "Module/Procgen/Rendering/BasicTerrainAsset.h"
#include "Module/Procgen/Types.h"

// TODO - We're going to make a TerrainRenderSystem
#include <vulkan/vulkan_core.h>

namespace procgen {
    struct TerrainRenderable;
}

namespace aveng {

    using TerrainGpuHandle = uint32_t;

    // Forward declarations to keep compile-times sane.
    struct FinalMeshCPU;
    struct VkRenderData;
    class ChunkManager;
    class EngineDevice;

    enum class TerrainRuntimeState : uint8_t
    {
        Unrequested,
        Requested,
        CpuReady,
        Uploading,
        Resident,
        Failed
    };

    struct TerrainChunkSlot
    {
        ChunkCoord coord{};
        TerrainRuntimeState state = TerrainRuntimeState::Unrequested;

        uint64_t requestId = 0;

        std::unique_ptr<procgen::TerrainRenderable> cpuRenderable;

        procgen::TerrainGpuChunk gpu;

        TerrainGpuHandle gpuHandle{};
    };

    /**
     * Game-facing interface for terrain generation / streaming.
     * Owns no terrain data; it just translates game intent into system work.
     */
    class TerrainController {
    public:
        explicit TerrainController(EngineDevice& engineDevice, VkRenderData& renderData, ChunkManager& chunks) noexcept;

        ~TerrainController();

        // Called once per tick by the engine (Midnight) so gameplay doesn't need to thread frame indices everywhere.
        void setFrameIndex(uint64_t frameIndex) noexcept;

        void setTerrainSettingsUbo(VkBuffer buffer, VkDeviceSize size) noexcept {
            settingsUboBuffer_ = buffer;
            settingsUboSize_ = size;
        }

        bool uploadTerrainChunkToGpu(TerrainChunkSlot& slot);

        bool writeChunkComputeDescriptorSet(
            procgen::TerrainPackedGpuData& packed,
            VkBuffer settingsUboBuffer,
            VkDeviceSize settingsUboSize);

        bool writeChunkGraphicsDescriptorSet(procgen::TerrainPackedGpuData& packed);

        void serviceCpuReadyChunks();

        void update(/*const Camera& camera*/);

        void render(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex);
        void renderDebug(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex);

        void cleanup();

        void cleanupOne(TerrainChunkSlot& slot);
        /**
         * Request the final renderable data for compute and graphics.
         * The renderable comprises a 3x3 region of space with up to a 5x5 region
         * in order to support computations (vertex normals, weights, adjacency data)
         *
         * Notes:
         * - This does not block/is async. ChunkManager hands you a shared_future.
         * - We keep the futures in an internal list for debugging / inspection.
         */
        void generateChunks(ChunkCoord start_coord, int cols, int rows);

        // void collectRenderable();

        void ensureChunkRequested(ChunkCoord coord);

        void drainCompletedTerrain();

        // Expose what was last requested for debug overlays, etc.
        // std::unique_ptr<procgen::TerrainRenderable> const& lastRequestedRenderable() const noexcept;

    private:
        static ChunkCoord offsetCoord(ChunkCoord base, int dx, int dz) noexcept;

        EngineDevice& engineDevice_;

        const int kMaxUploadsPerFrame = 2;

        VkRenderData& renderData_;

        VkBasicTerrainPushConstant pc;
        VkBasicTerrainDebugPC dpc;

        // Shared terrain compute settings UBO (owned by Renderer)
        VkBuffer settingsUboBuffer_ = VK_NULL_HANDLE;
        VkDeviceSize settingsUboSize_ = 0;

        // CEO
        ChunkManager* chunks_ = nullptr; // Primary Manager for Chunk Orchestration - non-owning
        // Managers
        procgen::ErosionManager erosionMgr_;
        uint64_t frameIndex_ = 0;

        std::unordered_map<ChunkCoord, TerrainChunkSlot, ChunkCoordHash> slots_;

        // std::unique_ptr<procgen::TerrainRenderable> lastRequested_;

    };
}