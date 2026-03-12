#pragma once
#include "avpch.h"
// #include "Core/Asset/AssetRegistry.h"
#include "Utils/Timer.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "CoreVK/VkRenderData.h"

// [!] Do not include SceneFacade or InstanceManager in this header.

namespace aveng {

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

        // TODO - THIS IS IN A HOT PATH (FramePacketBuilder)
        template<class Tag, class InstanceT>
        ModelId modelIdFromHandle(
            const std::vector<InstanceSlot<InstanceT>>& slots,
            const InstanceHandle<Tag>& h)
        {
            return slots[h.index].instance->common.modelId;
        }

        // ---- Deterministic key ordering helper ----

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

    enum class DrawFlavor : uint8_t {
        Static = 0,
        Animated = 1,
    };

    struct DrawBatch {
        DrawFlavor flavor{};
        ModelId    modelId{ 0 };

        uint32_t instanceCount{ 0 };
        uint32_t drawListOffset{ 0 };
        uint32_t skinMetaOffset{ 0 };

        uint32_t boneCount = 0;             // 0 for static
        uint32_t alignedInstanceCount = 0;  // for animated: ceil(n/32)*32
        uint32_t boneBaseOffset = 0;        // running prefix sum in bone matrices
    };

    // Frame Packet data is 100% ready draw routines
    struct FramePacket {
        uint32_t frameIndex = 0;
        uint64_t frameNumber = 0;

        // Guaranteed ordering: [0, staticInstanceCount) are static, [staticInstanceCount, total) are animated
        std::vector<AnyInstanceHandle> drawList;
        std::vector<uint32_t> pickIds;
        std::vector<AnyInstanceHandle> pickToHandle;
        // Guaranteed ordering: [0, staticBatchCount) are static batches, [staticBatchCount, total) are animated
        std::vector<DrawBatch> batches;

        // Delineation metadata - allows renderer to iterate by flavor without checking each batch
        uint32_t staticInstanceCount = 0;
        uint32_t animatedInstanceCount = 0;
        uint32_t staticBatchCount = 0;
        uint32_t animatedBatchCount = 0;

        // Instance data
        std::vector<glm::mat4> modelMats; // Each Model Matrix of this packet's draw list
        std::vector<NodeTransformData> nodeTransformData;  // Already padded to aligned counts

        std::span<const uint32_t> dirtyStaticSlots{};
        std::span<const uint32_t> dirtyAnimatedSlots{};
    };

    struct FramePacketBuildOptions {
        bool requireModelLoaded = true;
        bool deterministicBatches = false;

        // Choose your "source of truth" for ordering:
        // - instancesInOrder keeps a global per-pool order - do not use this.
        // - instancesPerModel groups by model first (order within model = vector order)
        bool preferInstancesInOrder = false;

        bool assignPickIds = false;
        uint32_t pickIdStart = 1;
    };

    class FramePacketBuilder final {
    public:
            
        void setModelQuery(const IModelQuery* q) { modelQ_ = q; }
        void setAnimQuery(IModelAnimQuery* aq) { animQ_ = aq; }
        void setFramesInFlight(int n) { framePackets_.resize(n); }
        const FramePacket& getFramePacket(int frameIndex) { return framePackets_[frameIndex]; }

        // This is a read-only view over existing InstanceManager containers (InstancePoolData)
        template<class Tag, class InstanceT>
        struct PoolInputs {
            using Handle = InstanceHandle<Tag>;
            using Slot = InstanceSlot<InstanceT>;

            const std::vector<Handle>* instancesInOrder = nullptr;
            const std::unordered_map<ModelId, std::vector<Handle>>* instancesPerModel = nullptr;
            std::vector<Slot>* slots = nullptr;

            std::span<const uint32_t> dirtySlots{};
        };

        FramePacketBuilder() = default;

        using BatchSortFn = std::function<bool(const DrawBatch&, const DrawBatch&)>;
        void setCustomBatchSort(BatchSortFn fn) { customBatchSort_ = std::move(fn); }

        template<class StaticInstanceT, class AnimatedInstanceT>
        FramePacket& build(
            const PoolInputs<StaticTag, StaticInstanceT>& stat,
            PoolInputs<AnimatedTag, AnimatedInstanceT>& anim,
            uint32_t frameIndex,
            uint64_t frameNumber, // Could become useful for profiling
            float deltaTime,
            const FramePacketBuildOptions& opt
#ifdef M_DEBUG
            , VkRenderData& renderData
#endif
        )
        {

#ifdef M_DEBUG
            mFramePacketTimer.start();
#endif
            // Ensure we have storage for this frame index
            if (frameIndex >= framePackets_.size()) {
                framePackets_.resize(frameIndex + 1);
            }

            nextPickId = 1;
            FramePacket& pkt = framePackets_[frameIndex];

            // Clear previous frame data - we could alternatively clean this up after each frame is completed in a GC step
            pkt.drawList.clear();
            pkt.batches.clear();    /// TODO can we reserve this too? - Only if you can determine batches in advance
            pkt.modelMats.clear();
            pkt.frameIndex = frameIndex;
            pkt.frameNumber = frameNumber;
            pkt.staticInstanceCount = 0;
            pkt.animatedInstanceCount = 0;
            pkt.staticBatchCount = 0;
            pkt.animatedBatchCount = 0;
            pkt.dirtyStaticSlots = stat.dirtySlots;
            pkt.dirtyAnimatedSlots = anim.dirtySlots;

            maxInstances = stat.slots->size() + anim.slots->size();
            pkt.drawList.reserve(maxInstances);
            pkt.pickIds.resize(maxInstances);
            pkt.pickToHandle.resize(maxInstances + 1); // +1 because pickId 0 is NullInstance

            // Defensive: required pointers
            if (!modelQ_ ||
                !stat.slots || !anim.slots ||
                (!stat.instancesInOrder && !stat.instancesPerModel) ||
                (!anim.instancesInOrder && !anim.instancesPerModel))
            {
                return pkt;
            }

            auto& statSlots = *stat.slots;
            auto& animSlots = *anim.slots;

            /// TODO - if no instances, just return empty packets. but maybe think that through

            // Model validation helper
            auto modelIsDrawable = [&](ModelId mid) -> bool {
                if (!opt.requireModelLoaded) return true;
                ModelMeta meta{};
                return modelQ_->isModelLoaded(mid, meta);
            };

            // ========== STATIC INSTANCES ==========

            auto processStaticFromOrdered = [&](const std::vector<StaticHandle>& orderedHandles) {
                ModelId currentModel = 0;
                bool haveBatch = false;
                DrawBatch batch{};
                batch.flavor = DrawFlavor::Static;

                for (const StaticHandle& h : orderedHandles) {
                    if (!isHandleAlive(statSlots, h)) { continue; }

                    const ModelId mid = modelIdFromHandle(statSlots, h);
                    if (!modelIsDrawable(mid)) { continue; }

                    // Ensures we create a different batch for each model.
                    if (!haveBatch || mid != currentModel) {
                        if (haveBatch) {
                            pkt.batches.push_back(batch);
                        }
                        haveBatch = true;
                        currentModel = mid;
                        batch = {};
                        batch.flavor = DrawFlavor::Static;
                        batch.modelId = mid;
                        batch.drawListOffset = static_cast<uint32_t>(pkt.drawList.size());
                        batch.instanceCount = 0;
                    }

                    pkt.drawList.emplace_back(h);
                    pkt.pickIds.push_back(nextPickId);
                    pkt.pickToHandle[nextPickId] = h;
                    ++nextPickId;

                    pkt.modelMats.push_back(statSlots[h.index].instance->common.modelMatrix());
                    batch.instanceCount++;
                }

                if (haveBatch) {
                    pkt.batches.push_back(batch);
                }
            };

            auto processStaticFromPerModel = [&](const std::unordered_map<ModelId, std::vector<StaticHandle>>& perModelMap) {
                std::vector<ModelId> model_ids;
                if (opt.deterministicBatches) {
                    model_ids = sortedModelKeys(perModelMap); // Numeric sorting on modelId
                }
                else {
                    model_ids.reserve(perModelMap.size());
                    for (const auto& kv : perModelMap) model_ids.push_back(kv.first);
                }

                for (ModelId mid : model_ids) {
                    auto it = perModelMap.find(mid);
                    if (it == perModelMap.end()) { continue; } // insanity check - more useful when sorting deterministicBatches
                    if (!modelIsDrawable(mid)) { continue; }

                    const auto& vec = it->second;
                    if (vec.empty()) { continue; }

                    DrawBatch batch{};
                    batch.flavor = DrawFlavor::Static;
                    batch.modelId = mid;
                    batch.drawListOffset = static_cast<uint32_t>(pkt.drawList.size());
                    batch.instanceCount = 0;

                    for (const StaticHandle& h : vec) {
                        if (!isHandleAlive(statSlots, h)) { continue; }

                        pkt.drawList.emplace_back(h);
                        pkt.pickIds.push_back(nextPickId);
                        pkt.pickToHandle[nextPickId] = h;
                        ++nextPickId;

                        pkt.modelMats.push_back(statSlots[h.index].instance->common.modelMatrix());
                        batch.instanceCount++;
                    }

                    if (batch.instanceCount > 0) {
                        pkt.batches.push_back(batch);
                    }
                }
            };

            
            // Build static portion
            if (opt.preferInstancesInOrder) {
                if (stat.instancesInOrder && !stat.instancesInOrder->empty()) {
                    processStaticFromOrdered(*stat.instancesInOrder);
                }
            }
            else {
                if (stat.instancesPerModel) {
                    processStaticFromPerModel(*stat.instancesPerModel);
                }
            }

            // Record static delineation
            pkt.staticInstanceCount = static_cast<uint32_t>(pkt.drawList.size());
            pkt.staticBatchCount = static_cast<uint32_t>(pkt.batches.size());
            

            // ========== ANIMATED INSTANCES ==========

            auto processAnimatedFromOrdered = [&](const std::vector<AnimatedHandle>& orderedHandles) {
                ModelId currentModel = 0;
                bool haveBatch = false;
                DrawBatch batch{};
                batch.flavor = DrawFlavor::Animated;

                for (const AnimatedHandle& h : orderedHandles) {
                    if (!isHandleAlive(animSlots, h)) { continue; }

                    const ModelId mid = modelIdFromHandle(animSlots, h);
                    if (!modelIsDrawable(mid)) { continue; }

                    if (!haveBatch || mid != currentModel) {
                        if (haveBatch) {
                            pkt.batches.push_back(batch);
                        }
                        haveBatch = true;
                        currentModel = mid;
                        batch = {};
                        batch.flavor = DrawFlavor::Animated;
                        batch.modelId = mid;
                        batch.drawListOffset = static_cast<uint32_t>(pkt.drawList.size());
                        batch.instanceCount = 0;
                    }

                    pkt.drawList.emplace_back(h);
                    pkt.pickIds.push_back(nextPickId);
                    pkt.pickToHandle[nextPickId] = h;
                    ++nextPickId;

                    pkt.modelMats.push_back(animSlots[h.index].instance->common.modelMatrix());

                    batch.instanceCount++;
                }

                if (haveBatch) {
                    pkt.batches.push_back(batch);
                }
            };

            auto processAnimatedFromPerModel = [&](const std::unordered_map<ModelId, std::vector<AnimatedHandle>>& perModelMap) {
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
                    if (it == perModelMap.end()) { continue; }
                    if (!modelIsDrawable(mid)) { continue; }

                    const auto& vec = it->second;
                    if (vec.empty()) { continue; }

                    DrawBatch batch{};
                    batch.flavor = DrawFlavor::Animated;
                    batch.modelId = mid;
                    batch.drawListOffset = static_cast<uint32_t>(pkt.drawList.size());
                    batch.instanceCount = 0;

                    for (const AnimatedHandle& h : vec) {
                        if (!isHandleAlive(animSlots, h)) { continue; }

                        pkt.drawList.emplace_back(h);
                        pkt.pickIds.push_back(nextPickId);
                        pkt.pickToHandle[nextPickId] = h;
                        ++nextPickId;

                        pkt.modelMats.push_back(animSlots[h.index].instance->common.modelMatrix());
                        batch.instanceCount++;
                    }

                    if (batch.instanceCount > 0) {
                        pkt.batches.push_back(batch);
                    }
                }
            };


            // Build animated portion
            if (opt.preferInstancesInOrder) {
                if (anim.instancesInOrder && !anim.instancesInOrder->empty()) {
                    processAnimatedFromOrdered(*anim.instancesInOrder);
                }
            }
            else {
                if (anim.instancesPerModel) {
                    processAnimatedFromPerModel(*anim.instancesPerModel);
                }
            }

            // Record animated delineation
            pkt.animatedInstanceCount = static_cast<uint32_t>(pkt.drawList.size()) - pkt.staticInstanceCount;
            pkt.animatedBatchCount = static_cast<uint32_t>(pkt.batches.size()) - pkt.staticBatchCount;
            
            // ========== POST-PROCESSING ==========
            /*
                Data Layout:
                batch[instance[nBones], instance[nBones], padding[nBones] ...32-1]
            */

            // Add bone information for animated batches (starting from last static batch offset)
            uint32_t boneBase = 0;
            for (uint32_t i = pkt.staticBatchCount; i < pkt.batches.size(); ++i) {
                auto& b = pkt.batches[i];
                ModelMeta meta{};
                
                // TODO - modelQ_ is a perf hit. Find a way to inline or use (const) ref without inheritance
                if (modelQ_->isModelLoaded(b.modelId, meta) && meta.boneCount > 0) {

                    /* These values are used to populate the graphics push constant */
                    b.boneCount = meta.boneCount;
                    b.alignedInstanceCount = ((b.instanceCount + 31) / 32) * 32; // ensure ceiling & matches compute invocation x
                    b.boneBaseOffset = boneBase;

                    /* This value is used with the compute push constant */
                    b.skinMetaOffset = meta.skinMetaIndex;

                    boneBase += b.boneCount * b.alignedInstanceCount;
                }

            }

            // Resize and fill nodeTransformData (boneBase is the total size needed)
            pkt.nodeTransformData.resize(boneBase);

            // Copy node transforms for each animated batch
            for (uint32_t batchIdx = pkt.staticBatchCount; batchIdx < pkt.batches.size(); ++batchIdx) {
                DrawBatch& b = pkt.batches[batchIdx];
                if (b.boneCount == 0) { continue; }

                // Copy each instance's node transforms
                for (uint32_t i = 0; i < b.instanceCount; ++i) {
                    AnyInstanceHandle& ah = pkt.drawList[b.drawListOffset + i];
                    AnimatedHandle h = std::get<AnimatedHandle>(ah); /// Look up std::get semantics

                    animSlots[h.index].instance->updateAnimation(deltaTime, *animQ_);
                    std::vector<NodeTransformData> boneSpan = animSlots[h.index].instance->getNodeTransformData();

#ifdef M_DEBUG
                    assert(boneSpan.size() == b.boneCount && "[FramepacketBuilder:BONE MISMATCH]");
#endif
                    std::copy(
                        boneSpan.begin(),
                        boneSpan.end(),
                        // First bone of instance `i` for this batch of instances
                        pkt.nodeTransformData.begin() + (b.boneBaseOffset + i * b.boneCount)
                    );
                }

                // Pad remaining slots up to alignedInstanceCount
                const NodeTransformData identityTrs{
                    glm::vec4(0.0f),
                    glm::vec4(1.0f),
                    glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                };

                // Pad the remaining "slab"
                for (uint32_t i = b.instanceCount; i < b.alignedInstanceCount; ++i) {
                    for (uint32_t bone = 0; bone < b.boneCount; ++bone) {
                        pkt.nodeTransformData[b.boneBaseOffset + i * b.boneCount + bone] = identityTrs;
                    }
                }
            }

#ifdef M_DEBUG
            renderData.rdFramePacketTime = mFramePacketTimer.stop();
#endif
            return pkt;
        }

    private:
        std::vector<FramePacket> framePackets_{};
        const IModelQuery* modelQ_ = nullptr;
        IModelAnimQuery* animQ_ = nullptr;
        BatchSortFn customBatchSort_{};
        size_t maxInstances = 0;
        uint32_t nextPickId = 1;
        Timer mFramePacketTimer{};

    };


}