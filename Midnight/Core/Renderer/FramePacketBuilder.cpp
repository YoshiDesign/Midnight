#include "FramePacketBuilder.h"

/*
Build order guarantees flavor-ordered output:
1. Process all static instances -> append to drawList, create static batches
2. Record staticInstanceCount = drawList.size(), staticBatchCount = batches.size()
3. Process all animated instances -> append to drawList, create animated batches
4. Record animatedInstanceCount = drawList.size() - staticInstanceCount
5. Record animatedBatchCount = batches.size() - staticBatchCount
6. Post-process animated batches for bone info
7. Assign pick IDs if requested
8. Store in framePackets_[frameIndex]
*/

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

    template<class StaticInstanceT, class AnimatedInstanceT>
    const FramePacket& FramePacketBuilder::build(
        const PoolInputs<StaticTag, StaticInstanceT>& stat,
        const PoolInputs<AnimatedTag, AnimatedInstanceT>& anim,
        uint32_t frameIndex,
        uint64_t frameNumber, // Could become useful for profiling
        const FramePacketBuildOptions& opt)
    {
        // Ensure we have storage for this frame index
        if (frameIndex >= framePackets_.size()) {
            framePackets_.resize(frameIndex + 1);
        }

        FramePacket& pkt = framePackets_[frameIndex];

        // Clear previous frame data - we could alternatively clean this up after each frame is completed in a GC step
        pkt.drawList.clear();
        pkt.batches.clear();
        pkt.frameIndex = frameIndex;
        pkt.frameNumber = frameNumber;
        pkt.staticInstanceCount = 0;
        pkt.animatedInstanceCount = 0;
        pkt.staticBatchCount = 0;
        pkt.animatedBatchCount = 0;
        pkt.dirtyStaticSlots = stat.dirtySlots;
        pkt.dirtyAnimatedSlots = anim.dirtySlots;

        // Defensive: required pointers
        if (!modelQ_ ||
            !stat.slots || !anim.slots ||
            (!stat.instancesInOrder && !stat.instancesPerModel) ||
            (!anim.instancesInOrder && !anim.instancesPerModel))
        {
            return pkt;
        }

        const auto& statSlots = *stat.slots;
        const auto& animSlots = *anim.slots;

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
                if (!isHandleAlive(statSlots, h)) continue;

                const ModelId mid = modelIdFromHandle(statSlots, h);
                if (!modelIsDrawable(mid)) continue;

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
                    batch.basePickId = 0;
                }

                pkt.drawList.emplace_back(h);
                pkt.worldMatrices.push_back(statSlots[h.index].instance->common.modelMatrix());
                batch.instanceCount++;
            }

            if (haveBatch) {
                pkt.batches.push_back(batch);
            }
        };

        auto processStaticFromPerModel = [&](const std::unordered_map<ModelId, std::vector<StaticHandle>>& perModelMap) {
            std::vector<ModelId> keys;
            if (opt.deterministicBatches) {
                keys = sortedModelKeys(perModelMap);
            } else {
                keys.reserve(perModelMap.size());
                for (const auto& kv : perModelMap) keys.push_back(kv.first);
            }

            for (ModelId mid : keys) {
                auto it = perModelMap.find(mid);
                if (it == perModelMap.end()) continue;
                if (!modelIsDrawable(mid)) continue;

                const auto& vec = it->second;
                if (vec.empty()) continue;

                DrawBatch batch{};
                batch.flavor = DrawFlavor::Static;
                batch.modelId = mid;
                batch.drawListOffset = static_cast<uint32_t>(pkt.drawList.size());
                batch.instanceCount = 0;
                batch.basePickId = 0;

                for (const StaticHandle& h : vec) {
                    if (!isHandleAlive(statSlots, h)) continue;
                    pkt.drawList.emplace_back(h);
                    pkt.worldMatrices.push_back(statSlots[h.index].instance->common.modelMatrix());
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
        } else {
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
                if (!isHandleAlive(animSlots, h)) continue;

                const ModelId mid = modelIdFromHandle(animSlots, h);
                if (!modelIsDrawable(mid)) continue;

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
                    batch.basePickId = 0;
                }

                pkt.drawList.emplace_back(h);
                pkt.worldMatrices.push_back(animSlots[h.index].instance->common.modelMatrix());

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
            } else {
                keys.reserve(perModelMap.size());
                for (const auto& kv : perModelMap) keys.push_back(kv.first);
            }

            for (ModelId mid : keys) {
                auto it = perModelMap.find(mid);
                if (it == perModelMap.end()) continue;
                if (!modelIsDrawable(mid)) continue;

                const auto& vec = it->second;
                if (vec.empty()) continue;

                DrawBatch batch{};
                batch.flavor = DrawFlavor::Animated;
                batch.modelId = mid;
                batch.drawListOffset = static_cast<uint32_t>(pkt.drawList.size());
                batch.instanceCount = 0;
                batch.basePickId = 0;

                for (const AnimatedHandle& h : vec) {
                    if (!isHandleAlive(animSlots, h)) continue;
                    pkt.drawList.emplace_back(h);
                    pkt.worldMatrices.push_back(animSlots[h.index].instance->common.modelMatrix());
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
        } else {
            if (anim.instancesPerModel) {
                processAnimatedFromPerModel(*anim.instancesPerModel);
            }
        }

        // Record animated delineation
        pkt.animatedInstanceCount = static_cast<uint32_t>(pkt.drawList.size()) - pkt.staticInstanceCount;
        pkt.animatedBatchCount = static_cast<uint32_t>(pkt.batches.size()) - pkt.staticBatchCount;

        // ========== POST-PROCESSING ==========

        // Add bone information for animated batches (starting at staticBatchCount)
        uint32_t boneBase = 0;
        for (uint32_t i = pkt.staticBatchCount; i < pkt.batches.size(); ++i) {
            auto& b = pkt.batches[i];
            ModelMeta meta{};
            if (modelQ_->isModelLoaded(b.modelId, meta) && meta.boneCount > 0) {
                b.boneCount = meta.boneCount;
                b.alignedInstanceCount = ((b.instanceCount + 31) / 32) * 32;
                b.boneBaseOffset = boneBase;
                boneBase += b.boneCount * b.alignedInstanceCount;
            }
        }

        // Resize and fill nodeTransformData (boneBase is the total size needed)
        pkt.nodeTransformData.resize(boneBase);

        // Copy node transforms for each animated batch
        for (uint32_t batchIdx = pkt.staticBatchCount; batchIdx < pkt.batches.size(); ++batchIdx) {
            const DrawBatch& b = pkt.batches[batchIdx];
            if (b.boneCount == 0) continue;

            // Copy each instance's node transforms
            for (uint32_t i = 0; i < b.instanceCount; ++i) {
                const AnyInstanceHandle& ah = pkt.drawList[b.drawListOffset + i];
                AnimatedHandle h = std::get<AnimatedHandle>(ah);

                auto boneSpan = animSlots[h.index].instance->getNodeTransformData();
                std::copy(
                    boneSpan.begin(),
                    boneSpan.end(),
                    pkt.nodeTransformData.begin() + (b.boneBaseOffset + i * b.boneCount)
                );
            }

            // Pad remaining slots up to alignedInstanceCount
            const NodeTransformData identityTrs{
                glm::vec4(0.0f),
                glm::vec4(1.0f),
                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
            };

            for (uint32_t i = b.instanceCount; i < b.alignedInstanceCount; ++i) {
                for (uint32_t bone = 0; bone < b.boneCount; ++bone) {
                    pkt.nodeTransformData[b.boneBaseOffset + i * b.boneCount + bone] = identityTrs;
                }
            }
        }

        // Assign pick IDs per batch if requested
        if (opt.assignPickIds) {
            uint32_t next = opt.pickIdStart;
            for (auto& b : pkt.batches) {
                b.basePickId = next;
                next += b.instanceCount;
            }
        }

        return pkt;
    }

    // Explicit instantiations can live here if you want to compile in one TU.
    // Otherwise include this .cpp in a compilation unit where instance types are known.
    // template const FramePacket& FramePacketBuilder::build<AvengInstance, AssimpInstance>(...);

}
