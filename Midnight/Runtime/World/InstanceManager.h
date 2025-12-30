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

        /* 
            Invariant: dirtyGpu.size() == slots.size() - don't abuse ensureDirtyArrays. Use it only after slots resize 
            Invariant: deletion of an instance does not remove its slot, it gets marked as dead/free. 
            Invariant: Handle.index == the slot's index in `slots` 
        */

        static TransformSettings toTransformSettings(const InstanceTransform& xf) {
            TransformSettings ts{};
            ts.worldPosition = xf.pos;
            ts.worldRotation = xf.rotEuler;
            ts.scale = xf.scale;

            // If swapYZ was only an import/build-time thing, it probably should NOT be cloned.
            // Default false is usually correct.
            ts.swapYZ = false;
            return ts;
        }

        static CreateSettingsFor<Tag> extractCreateSettings(const InstanceFor<Tag>& inst) {
            if constexpr (InstanceTypeFor<Tag>::kAnimated) {
                AnimatedCreateSettings cs{};
                cs.transform = toTransformSettings(inst.common.xf);
                cs.anim = inst.anim;              // or inst.animSettings()
                return cs;
            }
            else {
                return toTransformSettings(inst.common.xf);
            }
        }

        /* Current method for determining if transforms are dirty - epsilon compare would be safer but more expensive... */
        inline bool equalExact(const InstanceTransform& a, const InstanceTransform& b) {
            return a.pos == b.pos
                && a.rotEuler == b.rotEuler
                && a.scale == b.scale;
        }

        static void ensureDirtyArrays(InstancePoolData<Tag>& d) {
            if (d.dirtyGpu.size() != d.slots.size()) {
                d.dirtyGpu.resize(d.slots.size(), 0);
            }
        }

        void markGpuDirty(uint32_t idx) {
            ensureDirtyArrays(instanceData_);
            if (instanceData_.dirtyGpu[idx] == 0) {
                instanceData_.dirtyGpu[idx] = 1;
                instanceData_.dirtyGpuList.push_back(idx);
            }
        }

    public:

        using Handle = InstanceHandle<Tag>;
        using Instance = InstanceFor<Tag>;
        using Slot = InstanceSlot<Instance>;
        using InstanceData = InstancePoolData<Tag>;
        //using InstanceCallbacks = InstanceCallbacksPerPool<Handle>; // Unused

        //void setCallbacks(InstanceCallbacks callbacks) { // Unused
        //    callbacks_ = std::move(callbacks);
        //}

        // DEBUG
        void validateState() {
        
        }


        /* API - Danger */
        // InstanceData& data()                { return instanceData_; } // Not exactly a narrow API

        /* API -  */
        const InstanceData& data() const    { return instanceData_; } // Still not exactly a narrow API

        /* API -  */
        const std::vector<Handle>& instancesInOrder() const { return instanceData_.instancesInOrder; }

        /* API -  */
        const std::vector<Slot>& slots() const { return instanceData_.slots; }

        /* API - If renderer needs per-model groups: */ 
        const std::unordered_map<ModelId, std::vector<Handle>>& instancesPerModel() const { return instanceData_.instancesPerModel; }

        /* Ctor - Each instance manager manages a null instance */
        /* We pass an IModelQuery because instances need model constants */
        explicit InstanceManager(const IModelQuery& mq)
            : modelQuery_(mq) {
        
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

            // Note: We probably won't need to keep null instance in instancesPerModel anymore - test later
            instanceData_.instancesPerModel[NullModelId].emplace_back(handle);
            instanceData_.slots.emplace_back(slot);

            ensureDirtyArrays(instanceData_);

            // Also Note: we're not adding the Null instance to instancesInOrder or active
            // For this reason, never rely on a slot having the same index as a handle. 
            // Stick to the conventions and we're groovy
        
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

        /// Maybe
        // InstanceManager::reserveInstances(size_t capacity) (reserve vectors)
        /// Pick one
        // InstanceManager::preallocateSlots(size_t slotCount) (resize slots + fill free list + size dirty arrays)
        /// Maybe

        /* Get instance by Handle */
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
        (and doesn�t return a mutable reference that would be used to modify through a const object). */
        const Instance* get(Handle h) const {
            return const_cast<InstanceManager*>(this)->get(h);
        }

        /* This is a callback registered on the ModelLibrary */
        size_t purgeAllInstancesForModel(ModelId mid)
        {
            auto it = instanceData_.instancesPerModel.find(mid);
            if (it == instanceData_.instancesPerModel.end())
                return 0;

            std::vector<Handle> toDelete = it->second; // copy (delete mutates)
            size_t count = 0;

            for (const Handle& h : toDelete) {
                if (deleteInstance(h)) {
                    ++count; // ok if it returns false sometimes, but ideally it won�t
                }
                else {
                    std::cout << "[InstanceManager::purgeAllInstancesForModel] - Failed to delete instance " << h.index << " from modelId: " << mid << std::endl;
                }
            }

            // instancesPerModel[mid] should be gone by now (your deleteInstance erases it when empty)
            return count;
        }

        /*
        * See purgeAllInstancesForModel if you need outright deletion
         deleteInstance(span) expects the handle to refer to an external stable handle.
         Don't ever do something like: deleteInstance(instanceData_.instancesInOrder[i])
         or you'll potentially end up with UB. Handles are cheap. Copy them
        */
        bool deleteInstance(const Handle& h)
        {
            // --- 1) Validate handle/index/generation/alive ---
            if (h.generation == 0) { // Invalid generation
                std::cout << "InstanceManager::deleteInstance [0] Fail - no generation for handle\n";
                return false;
            }
            if (h.index >= instanceData_.slots.size()) { // Out of range
                std::cout << "InstanceManager::deleteInstance [0 again] Fail - handle index out of slots range\n";
                return false;
            }

            Slot& slot = instanceData_.slots[h.index];

            if (!slot.alive) { // no action needed
                std::cout << "InstanceManager::deleteInstance [1] Fail - Slot was not alive\n";
                return false;
            }
            if (slot.generation != h.generation) { // 
                std::cout << "InstanceManager::deleteInstance [2] Fail - No Generation in slot\n";
                return false;
            }
            if (!slot.instance.has_value()) {
                std::cout << "InstanceManager::deleteInstance [2] Fail - No instance in slot\n";
                return false;
            }

            // --- 2) Capture model key BEFORE destroying the instance ---
            // (The Instance stores AvengModel* this may become nullptr on destruction/reset.)
            Instance& inst = slot.instance.value(); // [!] One place we'd benefit from keeping ModelId in Slots
            ModelId mid = inst.modelId();
            // Long-winded but safest way to remove without accidental pollution
            if (auto it = instanceData_.instancesPerModel.find(mid); it != instanceData_.instancesPerModel.end()) {
                auto& handles = it->second;
                // Making use of the `InstanceHandle` overloaded operator==/!=. Uniqueness is (index, generation)
                handles.erase(std::remove(handles.begin(), handles.end(), h), handles.end());
                if (handles.empty()) instanceData_.instancesPerModel.erase(it); // optional cleanup
            }

            // Remove from ordered instances
            {
                auto& v = instanceData_.instancesInOrder;
                v.erase(std::remove(v.begin(), v.end(), h), v.end());
            }
            // Remove from active instances
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
                h.index) == instanceData_.free.end() 
                && "Duplicate index found in instanceData_.free!");
#endif
            // Insert a free slot index
            instanceData_.free.push_back(h.index);
            return true;
        }

        /*
         deleteInstances(span) expects the span to refer to external stable storage 
         don't ever do something like: deleteInstances(instanceData_.instancesInOrder[i])
         or you'll end up modifying the span internally. Handles are cheap, copy them
         and let deleteInstance take care of our storage types
        */
        void deleteInstances(std::span<const Handle> handles) {
            ensureDirtyArrays(instanceData_);
            for (const Handle& h : handles) {
                deleteInstance(h); 
            }
            /// Recall that deletion does not resize slots/handles,
            /// but does resize instancesInOrder, active, instancesPerModel[modelId]
        }

        Handle createInstance(const ModelId& mid, const ModelMeta& meta, const CreateSettingsFor<Tag>& settings) {

            /* TODO: When we get to FramePackets, I think we need to mark dirty on create and clone paths */

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
            markGpuDirty(slotIndex);
            // Via newly created index
            Slot& slot = instanceData_.slots[slotIndex];
            
            // Construct instance 
            auto& inst = slot.instance.emplace(); // Note: emplace is only for reset/create paths - bc it destroys what was there prior.
            slot.alive = true;

            // Initialize instance's model constants
            if constexpr (InstanceTypeFor<Tag>::kAnimated) {
                inst.init(mid, meta, settings.transform, settings.anim);
            }
            else {
                inst.init(mid, meta, settings);
            }

            // NOTE: generation already bumped on delete
            return Handle{
                slotIndex,
                slot.generation
            };
        }

        /* Note: adding multiple instances is handled by the Facade */
        Handle addInstanceOfModel(const  ModelId& mid, const CreateSettingsFor<Tag>& settings, const ModelMeta& m) {
            // Do not assert here.
            std::cout << "InstanceManager - Adding Instance...\n";
            Handle h = createInstance(mid, m, settings);
            instanceData_.instancesInOrder.push_back(h);
            instanceData_.instancesPerModel[mid].push_back(h);
            instanceData_.active.push_back(h.index);

            /*
                Note: at the moment the architecture leans towards being conservative.
                I'm pretty sure that vec::active<index> == vec::instancesInOrder<Handle>, they only differ by type
            */
            return h;
        }
        
        /*
        * Very tutorial with the get/set of settings
        * Cant this just be combined with cloneInstances?
        * There's no diff between this fn and passing numClones = 1 in cloneInstances
        * 
        * From ChatGuy:
        * you could also redefine CreateSettingsFor<StaticTag> to be InstanceTransform 
        * instead of TransformSettings if you want clones to be purely runtime-derived
        */
        Handle cloneInstance(const Handle& handle)
        {
            std::cout << "Cloning: Fetching clone pointer...\n";

            Instance* src = get(handle);
            if (!src) {
                // if constexpr (!/*failSoft?*/) { assert(false && "cloneInstance: invalid handle"); }
                std::cout << "[InstanceManager::cloneInstance] Invalid clone Handle!\n";
                return {}; // generation 0 -> �null�
            }
            const ModelId mid = src->common.modelId;

            // You likely already do this on create paths:
            ModelMeta meta{};
            if (!modelQuery_.tryGetModelMeta(mid, meta)) {
                // if constexpr (!/*failSoft?*/) { assert(false && "cloneInstance: model meta missing"); }
                return {};
            }

            CreateSettingsFor<Tag> settings = extractCreateSettings(*src);

            // --- Create a new slot or reuse a dead one ---
            uint32_t slotIndex = acquireSlotIndex();
            markGpuDirty(slotIndex);

            auto& slot = instanceData_.slots[slotIndex];

            if (slot.alive) {
                std::cout << "ANOMALY [1]\n";
            }

            // NOTE: generation already bumped on delete, or initialized to 1 for new slots
            slot.alive = true;

            auto& inst = slot.instance.emplace();

            if constexpr (InstanceTypeFor<Tag>::kAnimated) {
                inst.init(mid, meta, settings.transform, settings.anim);
            }
            else {
                inst.init(mid, meta, settings);
            }

            Handle h{ slotIndex, slot.generation };

            instanceData_.instancesInOrder.push_back(h);
            instanceData_.active.push_back(h.index);
            instanceData_.instancesPerModel[mid].push_back(h);

            // Mark GPU dirty for any new slots
 

            return h;
        }

        std::vector<Handle> cloneInstances(std::span<const Handle> handles)
        {

            std::vector<Handle> cloneHandles;

            for (int i = 0; i < handles.size(); i++) {
                cloneHandles.push_back(cloneInstance(handles[i]));
            }

            return cloneHandles;
        }

        bool setTransform(const Handle& h, const InstanceTransform& it)
        {
            if (!h) return false;
            if (h.index >= instanceData_.slots.size()) return false;

            Slot& slot = instanceData_.slots[h.index];
            if (!slot.alive) return false;
            if (slot.generation != h.generation) return false;
            if (!slot.instance.has_value()) return false;

            Instance& inst = *slot.instance;

            // Ignore no-op updates:
            if (equalExact(inst.common.xf, it)) {
                return true; // treat as success, but not dirty
            }
            // if (equalEps(inst.common.xf, it)) return true; // epsilon compare

            inst.common.xf = it;
            inst.common.dirty = true;

            // Mark dirty
            markGpuDirty(h.index);

            return true;
        }

        void setTransforms(
            std::span<const Handle> handles,
            std::span<const InstanceTransform> transforms)
        {
            std::cout << "[InstanceManager] Setting transforms!\n";
            if (handles.size() != transforms.size()) {
                std::cout << "[InstanceManager] Transform/Handle spans were not the same size!\n";
                return;
            }
#ifdef M_DEBUG
            // This should never resize here
            if (instanceData_.dirtyGpu.size() != instanceData_.slots.size()) {
                std::cout << "[InstanceManager] dirtyGpu Resized on transform data? wtf m8\n";
                ensureDirtyArrays(instanceData_);
            }
#endif
            for (size_t i = 0; i < handles.size(); ++i) {
                // Inline the work if you want slightly better perf,
                // but calling setTransform is fine for now because it�s O(1) and branchy anyway.
                setTransform(handles[i], transforms[i]);
            }
        }

        void deleteAllInstancesForModel(ModelId mid)
        {
            auto it = instanceData_.instancesPerModel.find(mid);
            if (it == instanceData_.instancesPerModel.end()) {
                return; // No recorded instances from this modelId
                // TODO: Could validate against handles and slots here
            }

            // Copy first because deleteInstance() mutates instancesPerModel[mid]
            std::vector<Handle> toDelete = it->second;

            deleteInstances(toDelete); // your existing span-based batch delete
            // deleteInstances will erase the map entry when it becomes empty (per your code)
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
        const IModelQuery& modelQuery_; // This is "technically" the entire Renderer. Just a constrained API from it
        // InstanceCallbacks   callbacks_{}; // Unused - enables callbacks to be created on behalf of this class. See: ModelAndInstanceData.h -> InstanceCallbacksPerPool
        InstanceData        instanceData_{};
    };
}