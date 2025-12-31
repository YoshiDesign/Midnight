#pragma once
#include "avpch.h"
// #include "Core/Modeling/ModelRegistry.h"
#include "Core/Modeling/ModelAndInstanceData.h"

// [!] Do not include SceneFacade or InstanceManager in this header.

namespace aveng {

    enum class DrawFlavor : uint8_t {
        Static = 0,
        Animated = 1,
    };

    struct DrawBatch {
        DrawFlavor flavor{};
        ModelId    modelId{ 0 };

        uint32_t instanceCount{ 0 };
        uint32_t drawListOffset{ 0 };

        // Optional (for editor picking): pickId = basePickId + gl_InstanceIndex
        uint32_t basePickId{ 0 };

        uint32_t boneCount = 0;             // 0 for static
        uint32_t alignedInstanceCount = 0;  // for animated: ceil(n/32)*32
        uint32_t boneBaseOffset = 0;        // running prefix sum in bone matrices
    };

    struct FramePacket {
        uint32_t frameIndex = 0;
        uint64_t frameNumber = 0;

        // Guaranteed ordering: [0, staticInstanceCount) are static, [staticInstanceCount, total) are animated
        std::vector<AnyInstanceHandle> drawList;
        // Guaranteed ordering: [0, staticBatchCount) are static batches, [staticBatchCount, total) are animated
        std::vector<DrawBatch> batches;

        // Delineation metadata - allows renderer to iterate by flavor without checking each batch
        uint32_t staticInstanceCount = 0;
        uint32_t animatedInstanceCount = 0;
        uint32_t staticBatchCount = 0;
        uint32_t animatedBatchCount = 0;

        // IN PREPARATION FOR FUTURE REFACTOR : Pre-extracted instance data (ready for GPU upload)
        std::vector<glm::mat4> worldMatrices;
        std::vector<NodeTransformData> nodeTransformData;  // Already padded to aligned counts

        std::span<const uint32_t> dirtyStaticSlots{};
        std::span<const uint32_t> dirtyAnimatedSlots{};
    };

    struct FramePacketBuildOptions {
        bool requireModelLoaded = true;
        bool deterministicBatches = true;

        // Choose your "source of truth" for ordering:
        // - instancesInOrder keeps a global per-pool order
        // - instancesPerModel groups by model first (order within model = vector order)
        bool preferInstancesInOrder = false;

        // If true, assigns basePickId per batch (1..N). If false, basePickId=0.
        bool assignPickIds = false;
        uint32_t pickIdStart = 1;
    };

    class FramePacketBuilder final {
    public:

        void setModelQuery(const IModelQuery* q) { modelQ_ = q; }
        void setFramesInFlight(int n) { framePackets_.resize(n); }
        const FramePacket& getFramePacket(int frameIndex) { return framePackets_[frameIndex]; }

        // This is a read-only view over existing InstanceManager containers (InstancePoolData)
        template<class Tag, class InstanceT>
        struct PoolInputs {
            using Handle = InstanceHandle<Tag>;
            using Slot = InstanceSlot<InstanceT>;

            const std::vector<Handle>* instancesInOrder = nullptr;
            const std::unordered_map<ModelId, std::vector<Handle>>* instancesPerModel = nullptr;
            const std::vector<Slot>* slots = nullptr;

            std::span<const uint32_t> dirtySlots{};
        };

        FramePacketBuilder() = default;

        using BatchSortFn = std::function<bool(const DrawBatch&, const DrawBatch&)>;
        void setCustomBatchSort(BatchSortFn fn) { customBatchSort_ = std::move(fn); }

        template<class StaticInstanceT, class AnimatedInstanceT>
        const FramePacket& build(
            const PoolInputs<StaticTag, StaticInstanceT>& stat,
            const PoolInputs<AnimatedTag, AnimatedInstanceT>& anim,
            uint32_t frameIndex,
            uint64_t frameNumber,
            const FramePacketBuildOptions& opt = {});

    private:
        std::vector<FramePacket> framePackets_{};
        const IModelQuery* modelQ_ = nullptr;
        BatchSortFn customBatchSort_{};
    };


}