#include "FramePacketBuilder.h"

namespace aveng {

    // Include the actual instance types here
    // #include "AvengInstance.h"
    // #include "AssimpInstance.h"

    //struct ModelMeta {
    //    bool animated = false;
    //    uint32_t boneCount = 0;
    //    // glm::mat4 root;  // not needed for packet building itself
    //};

    //struct IModelQuery {
    //    virtual ~IModelQuery() = default;
    //    virtual bool tryGetModelMeta(ModelId id, ModelMeta& out) const = 0;
    //    virtual bool isModelLoaded(ModelId id, ModelMeta& out) const = 0;
    //    virtual bool isModelAnimated(ModelId id, ModelMeta& out) const = 0;
    //};

    namespace {

        // ---- Handle validation ----

        template<class Tag, class InstanceT>
        bool isHandleAlive(
            const std::vector<InstanceSlot<InstanceT>>& slots,
            const InstanceHandle<Tag>& h)
        {
            if (h.generation == 0) return false;
            if (h.index >= slots.size()) return false;
            const auto& slot = slots[h.index];
            if (!slot.alive) return false;
            if (slot.generation != h.generation) return false;
            if (!slot.instance.has_value()) return false;
            return true;
        }

        template<class Tag, class InstanceT>
        ModelId modelIdFromHandle(
            const std::vector<InstanceSlot<InstanceT>>& slots, 
            const InstanceHandle<Tag>& h)
        {
            // Call only after isHandleAlive == true. Optional must not be empty here to use ptr syntax
            return slots[h.index].instance->common.modelId;
        }

        // ---- Deterministic key ordering helpers. Only used if deterministicBatches option is true ----

        template<class HandleT>
        std::vector<ModelId> sortedModelKeys(const std::unordered_map<ModelId, std::vector<HandleT>>& perModel)
        {
            std::vector<ModelId> keys;
            keys.reserve(perModel.size());
            for (const auto& kv : perModel) keys.push_back(kv.first);
            std::sort(keys.begin(), keys.end());
            return keys;
        }

    }

    template<class StaticInstanceT, class AnimatedInstanceT>
    FramePacket FramePacketBuilder::build(
        const Inputs& in,
        const PoolInputs<StaticTag, StaticInstanceT>& stat,
        const PoolInputs<AnimatedTag, AnimatedInstanceT>& anim,
        uint32_t frameIndex,
        uint64_t frameNumber,
        const FramePacketBuildOptions& opt)
    {
        FramePacket pkt;
        pkt.frameIndex = frameIndex;
        pkt.frameNumber = frameNumber;

        pkt.dirtyStaticSlots = stat.dirtySlots;
        pkt.dirtyAnimatedSlots = anim.dirtySlots;

        // Defensive: required pointers
        if (!in.modelQ ||
            !stat.slots || !anim.slots ||
            (!stat.instancesInOrder && !stat.instancesPerModel) ||
            (!anim.instancesInOrder && !anim.instancesPerModel))
        {
            // Return empty packet; caller can treat as "draw nothing"
            return pkt;
        }

        const auto& statSlots = *stat.slots;
        const auto& animSlots = *anim.slots;

        auto modelIsDrawable = [&](ModelId mid, DrawFlavor flavor) -> bool {
            if (!opt.requireModelLoaded) return true;

            ModelMeta meta{};
            if (!in.modelQ->isModelLoaded(mid, meta)) return false;

            // Optional sanity: if flavor contradicts meta, you can choose to filter or allow.
            // I recommend: allow both, because you may have static instances of animated models.
            // If you want strictness:
            // if (flavor == DrawFlavor::Animated && !meta.animated) return false;

            (void)flavor;
            return true;
        };

        // Emit batches from a stream of handles that is already in canonical order.
        auto emitFromOrderedHandles = [&](auto flavor,
            const auto& slots,
            const auto& orderedHandles)
        {
            using HandleT = std::decay_t<decltype(orderedHandles.front())>;

            // We batch adjacent handles by (modelId, flavor).
            // This preserves per-pool order and minimizes state changes *if* your pool order is model-grouped.
            // If pool order is arbitrary, you’ll get more batches — still correct.
            ModelId currentModel = 0;
            bool haveBatch = false;

            DrawBatch batch{};
            batch.flavor = flavor;

            for (const HandleT& h : orderedHandles) {
                if (!isHandleAlive(slots, h)) continue;

                const ModelId mid = modelIdFromHandle(slots, h);
                if (!modelIsDrawable(mid, flavor)) continue;

                if (!haveBatch || mid != currentModel) {
                    // close previous
                    if (haveBatch) {
                        pkt.batches.push_back(batch);
                    }

                    // start new
                    haveBatch = true;
                    currentModel = mid;

                    batch = {};
                    batch.flavor = flavor;
                    batch.modelId = mid;
                    batch.drawListOffset = static_cast<uint32_t>(pkt.drawList.size());
                    batch.instanceCount = 0;
                    batch.basePickId = 0; // fill later
                }

                // Append to canonical draw list
                if constexpr (std::is_same_v<HandleT, StaticHandle>) {
                    pkt.drawList.emplace_back(h);
                }
                else {
                    pkt.drawList.emplace_back(h);
                }
                batch.instanceCount++;
            }

            if (haveBatch) {
                pkt.batches.push_back(batch);
            }
        };

        // Emit from per-model grouping: batches are naturally per model.
        auto emitFromPerModel = [&](auto flavor,
            const auto& slots,
            const auto& perModelMap)
        {
            using HandleT = typename std::decay_t<decltype(perModelMap)>::mapped_type::value_type;

            std::vector<ModelId> keys;
            if (opt.deterministicBatches) {
                keys = sortedModelKeys(perModelMap);
            }
            else {
                keys.reserve(perModelMap.size());
                for (const auto& kv : perModelMap) keys.push_back(kv.first);
            }

            for (ModelId mid : keys) {
                auto it = perModelMap.find(mid);
                if (it == perModelMap.end()) continue;

                if (!modelIsDrawable(mid, flavor)) continue;

                const auto& vec = it->second;
                if (vec.empty()) continue;

                DrawBatch batch{};
                batch.flavor = flavor;
                batch.modelId = mid;
                batch.drawListOffset = static_cast<uint32_t>(pkt.drawList.size());
                batch.instanceCount = 0;
                batch.basePickId = 0;

                for (const HandleT& h : vec) {
                    if (!isHandleAlive(slots, h)) continue;

                    // Optional: cross-check model id matches map key
                    // (cheap assertion-style check)
                    // if (modelIdFromHandle(slots, h) != mid) continue;

                    if constexpr (std::is_same_v<HandleT, StaticHandle>) {
                        pkt.drawList.emplace_back(h);
                    }
                    else {
                        pkt.drawList.emplace_back(h);
                    }
                    batch.instanceCount++;
                }

                if (batch.instanceCount > 0) {
                    pkt.batches.push_back(batch);
                }
            }
        };

        // --- Build order: choose policy ---
        if (opt.preferInstancesInOrder) {
            if (stat.instancesInOrder && !stat.instancesInOrder->empty()) {
                emitFromOrderedHandles(DrawFlavor::Static, statSlots, *stat.instancesInOrder);
            }
            if (anim.instancesInOrder && !anim.instancesInOrder->empty()) {
                emitFromOrderedHandles(DrawFlavor::Animated, animSlots, *anim.instancesInOrder);
            }
        }
        else {
            if (stat.instancesPerModel) {
                emitFromPerModel(DrawFlavor::Static, statSlots, *stat.instancesPerModel);
            }
            if (anim.instancesPerModel) {
                emitFromPerModel(DrawFlavor::Animated, animSlots, *anim.instancesPerModel);
            }
        }

        // Optional: allow custom global batch sorting (rarely needed if you preserve submission order).
        // If you do sort batches, you MUST also rebuild drawList accordingly (or store per-batch handle arrays).
        // Therefore, by default we do NOT sort batches here.
        if (customBatchSort_) {
            // NOTE: Sorting batches alone would invalidate (drawListOffset, instanceCount) mapping.
            // Only enable this if you also change representation to store handles per batch.
            // For now: ignore custom sort to preserve correctness.
            // std::sort(pkt.batches.begin(), pkt.batches.end(), customBatchSort_);
        }

        // Assign pick IDs per batch (basePickId), if requested.
        if (opt.assignPickIds) {
            uint32_t next = opt.pickIdStart; // usually 1
            for (auto& b : pkt.batches) {
                b.basePickId = next;
                next += b.instanceCount;
            }
        }

        return pkt;
    }

    // Explicit instantiations can live here if you want to compile in one TU.
    // Otherwise include this .cpp in a compilation unit where instance types are known.
    // template FramePacket FramePacketBuilder::build<AvengInstance, AssimpInstance>(...);


}