#pragma once
#include "avpch.h"
#include "Services/ModelServices.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/Modeling/InstanceSettings.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"
#include "Core/Modeling/ModelRegistry.h"
#include "Core/aveng_model.h"

namespace aveng {

    class AvengModel;
    class EngineDevice;

    template<class Tag>
    class InstanceManager {
    public:

        using Handle = InstanceHandle<Tag>;
        using Instance = InstanceFor<Tag>;
        using Slot = InstanceSlot<Instance>;
        using InstanceData = InstancePoolData<Tag>;
        using InstanceCallbacks = InstanceCallbacksPerPool<Handle>;

        void setCallbacks(InstanceCallbacks callbacks) {
            callbacks_ = std::move(callbacks);
        }

        // DEBUG
        void validateState() {
        
        }

        /* API - Danger */
        // InstanceData& data()                { return instanceData_; } // Not exactly a narrow API

        /* API -  */
        const InstanceData& data() const    { return instanceData_; } // Not exactly a narrow API

        /* API -  */
        const std::vector<Handle>& instancesInOrder() const { return instanceData_.instancesInOrder; }

        /* API -  */
        const std::vector<Slot>& slots() const { return instanceData_.slots; }

        /* API - If renderer needs per-model groups: */ 
        const std::unordered_map<ModelId, std::vector<Handle>>& instancesPerModel() const { return instanceData_.instancesPerModel; }

        /* Ctor - Each instance manager manages a null instance */
        /* We pass an IModelQuery because instances need model constants */
        explicit InstanceManager(const IModelQuery& models)
            : models_(models) {
        
            // Note: Manually setting the modelId for the null-instance
            Instance nullInstance = Instance();
            nullInstance.setModelId(NullModelId);

            Slot slot = {
                nullInstance, // instance
                1, // generation
                true // alive - arbitrary
            };

            Handle handle = {
                instanceData_.slots.size(), // index
                1 // generation
            };

            // NOTE: We probably won't need to keep null instance in instancesPerModel anymore - test later
            instanceData_.instancesPerModel[NullModelId].emplace_back(handle);
            instanceData_.slots.emplace_back(slot);
        
        }

    private:

        /* Get a free slot's index or create a new one and return its index */
        uint32_t acquireSlotIndex() {

            if (!instanceData_.free.empty()) {
                // Acquire an unused slot
                uint32_t idx = instanceData_.free.back();
                instanceData_.free.pop_back();
                return idx;
            }

            uint32_t idx = static_cast<uint32_t>(instanceData_.slots.size());
            instanceData_.slots.emplace_back(); // default Slot: instance=nullopt, alive=false, generation=1
            return idx;
        }

    public:

        /* get() the instance from a slot, given a heandle - With a lot validation */
        Instance* get(Handle h) {
            if (h.generation == 0) return nullptr;
            if (h.index >= instanceData_.slots.size()) return nullptr;

            auto& slot = instanceData_.slots[h.index];
            if (!slot.alive) return nullptr;
            if (slot.generation != h.generation) return nullptr;
            if (!slot.instance.has_value()) return nullptr;

            return &slot.instance.value();
        }

        /* get() const - only safe if the non-const get() does not actually modify the manager 
        (and doesn’t return a mutable reference that would be used to modify through a const object). */
        const Instance* get(Handle h) const {
            return const_cast<InstanceManager*>(this)->get(h);
        }

        /*
         deleteInstance(span) expects the handle to refer to an external stable handle.
         Don't ever do something like: deleteInstance(instanceData_.instancesInOrder[i])
         or you'll potentially end up with UB. Handles are cheap. Copy them
        */
        void deleteInstance(const Handle& h)
        {
            // --- 1) Validate handle/index/generation/alive ---
            if (h.generation == 0) {
                std::cout << "InstanceManager::deleteInstance [0] Fail - no generation for handle\n";
                return;
            }
            if (h.index >= instanceData_.slots.size()) {
                std::cout << "InstanceManager::deleteInstance [0 again] Fail - handle index out of slots range\n";
                return;
            }

            Slot& slot = instanceData_.slots[h.index];

            if (!slot.alive) {
                std::cout << "InstanceManager::deleteInstance [1] Fail - Slot was not alive\n";
                return;
            }
            if (slot.generation != h.generation) {
                std::cout << "InstanceManager::deleteInstance [2] Fail - No Generation in slot\n";
                return;
            }
            if (!slot.instance.has_value()) {
                std::cout << "InstanceManager::deleteInstance [2] Fail - No instance in slot\n";
                return;
            }

            // --- 2) Capture model key BEFORE destroying the instance ---
            // (The Instance stores AvengModel* this may become nullptr on destruction/reset.)
            Instance& inst = slot.instance.value();
            ModelId mid = inst.modelId();
            // Long-winded but safest way to remove without accidental pollution
            if (auto it = instanceData_.instancesPerModel.find(mid); it != instanceData_.instancesPerModel.end()) {
                auto& vec = it->second;
                vec.erase(std::remove(vec.begin(), vec.end(), h), vec.end());
                if (vec.empty()) instanceData_.instancesPerModel.erase(it); // optional cleanup
            }

            // --- 3) Remove handle from "instancesInOrder" ---
            // O(n), but simple and correct. You can later optimize with a back-pointer index.
            // For now, instances are mostly reused, and only deleted upon cleanup.
            {
                auto& v = instanceData_.instancesInOrder;
                v.erase(std::remove(v.begin(), v.end(), h), v.end());
            }
            {
                auto& v = instanceData_.active;
                v.erase(std::remove(v.begin(), v.end(), h.index), v.end());
            }

            // --- 4) Destroy the ~Instance()
            slot.instance.reset();

            // --- 5) Invalidate slot + recycle index ---
            slot.alive = false;

            // Bump generation so old handles become stale.
            slot.generation = (slot.generation == std::numeric_limits<uint32_t>::max())
                ? 1u
                : slot.generation + 1u;

#ifdef M_DEBUG
            // DEBUG
            assert(std::find(instanceData_.free.begin(), instanceData_.free.end(), 
                h.index) != instanceData_.free.end() 
                && "Duplicate index found in instanceData_.free!");
#endif
            instanceData_.free.push_back(h.index);

        }

        /*
         deleteInstances(span) expects the span to refer to external stable storage 
         don't ever do something like: deleteInstances(instanceData_.instancesInOrder[i])
         or you'll end up modifying the span internally. Handles are cheap, copy them
         and let deleteInstance take care of our storage types
        */
        void deleteInstances(std::span<const Handle> handles) {
            for (const Handle& h : handles) {
                deleteInstance(h);
            }
        }

        Handle createInstance(const ModelId& mid, const ModelMeta& meta, const CreateSettingsFor<Tag>& settings) {
#ifdef M_DEBUG
            assert(mid != 0 && "Attempting to create instance from invalid modelRef");
#endif

#ifdef M_DEBUG
            // Optional safety: don't let the wrong pool create the wrong kind of model
            if constexpr (TagTraits<Tag>::kAnimated) {
                if (!meta.animated) {
                    std::cout << "[GLARING ERROR] wtf 1 - InstanceManager Anomaly\n";
                    return {};
                }
            }
            else {
                if (meta.animated) {
                    std::cout << "[GLARING ERROR] wtf 2 - InstanceManager Anomaly\n";
                    return {};
                }
                
            }
#endif
            std::cout << "Creating Instance Datas...\n";
            uint32_t slotIndex = acquireSlotIndex(); // side-effects: Creates a slot if none are free

            // Via newly created index
            Slot& slot = instanceData_.slots[slotIndex];
            
            // Construct instance 
            auto& inst = slot.instance.emplace(); // Note: emplace is only for create paths.
            slot.alive = true;

            // Initialize instance's model constants
            if constexpr (InstanceTypeFor<Tag>::kAnimated) {
                inst.init(mid, meta, settings.transform, settings.anim);
            }
            else {
                inst.init(mid, meta, settings);
            }

            // Technically we could have used models_.isModelAnimated() but this is more educational and one less stack frame, so...
            if constexpr (TagTraits<Tag>::kAnimated) {
                inst.resizeNodeTransformData(meta.boneCount); // TODO
            }

            // NOTE: generation already bumped on delete
            return Handle{
                slotIndex,
                slot.generation
            };
        }

        Handle addInstanceOfModel(const  ModelId& mid, const CreateSettingsFor<Tag>& settings, const ModelMeta& m) {
            // Do not assert here.
            std::cout << "InstanceManager - Adding Instance...\n";
            Handle h = createInstance(mid, m, settings);
            instanceData_.instancesInOrder.push_back(h);
            instanceData_.instancesPerModel[mid].push_back(h);
        }
        
        ///* Add multiple instances of a given model - Note, use span when usage is minimal - no resizes, etc. */
        //std::vector<Handle> addInstancesOfModel(const ModelRef& modelRef, std::span<const InstanceSettings> settings, unsigned int n) {

        //    std::cout << "InstanceManager - Adding Multiple Instance...\n";
        //    std::vector<Handle> out;
        //    out.reserve(n);

        //    for (unsigned i = 0; i < n; ++i) {
        //        const InstanceSettings& s = settings[i % settings.size()];
        //        out.push_back(addInstanceOfModel(modelRef, s));
        //    }
        //    return out;
        //}

        /*
        * Very tutorial with the get/set of settings
        * Cant this just be combined with cloneInstances?
        * There's no diff between this fn and passing numClones = 1 in cloneInstances
        */
        Handle cloneInstance(const Handle& handle) {
            
            std::cout << "Cloning: Fetching clone pointer...\n";
            // Fetch the clone
            Instance* clone = get(handle);
            ModelId mid = clone->modelId();
            CreateSettingsFor<Tag> settings = clone->instanceSettings();

            // New slot
            uint32_t slotIndex = acquireSlotIndex();
            auto& slot = instanceData_.slots[slotIndex];

            if (slot.alive) {
                // should never happen if your free list is correct
                std::cout << "ANOMALY [1]" << std::endl;
            }

            // bump generation on reuse
            slot.generation = std::max(slot.generation + 1, 1u);
            slot.alive = true;

            auto& inst = slot.instance.emplace(); // Note: emplace is only for create paths.
            if constexpr (InstanceTypeFor<Tag>::kAnimated) {
                inst.init(mid, meta, settings.transform, settings.anim);
            }
            else {
                inst.init(mid, meta, settings);
            }
            
            Handle h{ slotIndex, slot.generation };
  
            instanceData_.instancesInOrder.push_back(h);
            instanceData_.instancesPerModel[mid].push_back(h);
            clone = nullptr;
            return h;
        }

        std::vector<Handle> cloneInstances(const Handle& handle, unsigned int nClones)
        {
            std::cout << "Cloning: Requested " << nClones << " clones\n";
            if (nClones == 0) nClones = 1;

            std::vector<Handle> cloneHandles;

            for (int i = 0; i < nClones; i++) {
                cloneHandles.push_back(cloneInstance(handle));
            }

            return cloneHandles;
        }

        /// Set transform for one instance.
        void setTransform(AnyInstanceHandle h, const InstanceTransform& t) {
        
        }

        /// Optional convenience for batch transforms (future-friendly).
        void setTransforms(std::span<const AnyInstanceHandle> handles,
            std::span<const InstanceTransform> transforms) {
        
        }

        /* Some settings modifiers */
        inline int randRotate() { return std::rand() % 360 - 180;  }

        unsigned int randomAnimClip(size_t animClipSize) {
            int xPos = std::rand() % 50 - 25;
            int zPos = std::rand() % 50 - 25;
            int rotation = std::rand() % 360 - 180;
            int clipNr = std::rand() % animClipSize;
            return clipNr;
        }

        glm::vec3 randomPosFromOrigin(glm::vec3 origin) {
            int xPos = (std::rand() % 50 - 25) + origin.x;
            int zPos = (std::rand() % 50 - 25) + origin.z;
            return { xPos, 0.0f, zPos };
        }

    private:
        const IModelQuery&  models_; // This is "technically" the entire Renderer. Just a constrained API from it
        InstanceCallbacks   callbacks_{};
        InstanceData        instanceData_{};
    };
}