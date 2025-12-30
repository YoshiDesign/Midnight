#pragma once
#include "avpch.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/Modeling/ModelRegistry.h"
namespace aveng {

    // using ModelId = uint32_t;

    struct ModelMeta;
    struct IModelQuery;

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
    };

    struct FramePacket {
        uint32_t frameIndex = 0;
        uint64_t frameNumber = 0;

        std::vector<AnyInstanceHandle> drawList;
        std::vector<DrawBatch> batches;

        std::span<const uint32_t> dirtyStaticSlots{};
        std::span<const uint32_t> dirtyAnimatedSlots{};
    };

    struct FramePacketBuildOptions {
        bool requireModelLoaded = true;
        bool deterministicBatches = true;

        // Choose your "source of truth" for ordering:
        // - instancesInOrder keeps a global per-pool order
        // - instancesPerModel groups by model first (order within model = vector order)
        bool preferInstancesInOrder = true;

        // If true, assigns basePickId per batch (1..N). If false, basePickId=0.
        bool assignPickIds = false;
        uint32_t pickIdStart = 1;
    };

    class FramePacketBuilder final {
    public:

        // This is a read-only view over existing InstanceManager containers (InstancePoolData )
        template<class Tag, class InstanceT>
        struct PoolInputs {
            using Handle = InstanceHandle<Tag>;
            using Slot = InstanceSlot<InstanceT>;

            const std::vector<Handle>* instancesInOrder = nullptr;
            const std::unordered_map<ModelId, std::vector<Handle>>* instancesPerModel = nullptr;
            const std::vector<Slot>* slots = nullptr;

            std::span<const uint32_t> dirtySlots{};
        };

        struct Inputs {
            // Static pool
            // You'll fill InstanceT with your concrete types (AvengInstance / AssimpInstance).
            // This header stays generic; the .cpp will include the concrete instance headers.
            // We therefore declare these as templates in build(...) below instead.
            const IModelQuery* modelQ = nullptr;
        };

        FramePacketBuilder() = default;

        using BatchSortFn = std::function<bool(const DrawBatch&, const DrawBatch&)>;
        void setCustomBatchSort(BatchSortFn fn) { customBatchSort_ = std::move(fn); }

        template<class StaticInstanceT, class AnimatedInstanceT>
        FramePacket build(
            const Inputs& in,
            const PoolInputs<StaticTag, StaticInstanceT>& stat,
            const PoolInputs<AnimatedTag, AnimatedInstanceT>& anim,
            uint32_t frameIndex,
            uint64_t frameNumber,
            const FramePacketBuildOptions& opt = {});

    private:
        BatchSortFn customBatchSort_{};
    };


}