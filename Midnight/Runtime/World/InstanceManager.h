#pragma once
#include "avpch.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/Modeling/InstanceSettings.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"
#include "Core/Modeling/ModelRegistry.h"
#include "Core/aveng_model.h"
namespace aveng {

    /*
    * TODO Maybe - safety checks
    * Slot* tryGetSlot(Handle h) {
            if (!h) return nullptr;
            if (h.index >= instanceData_.slots.size()) return nullptr;
            auto& slot = instanceData_.slots[h.index];
            if (!slot.alive) return nullptr;
            if (slot.generation != h.generation) return nullptr;
            return &slot;
        }
    */

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

        /* */
        InstanceData& data()                { return instanceData_; }

        /* */
        const InstanceData& data() const    { return instanceData_; }

        /* */
        const std::vector<Handle>& instancesInOrder() const { return instanceData_.instancesInOrder; }

        /* */
        const std::vector<Slot>& slots() const { return instanceData_.slots; }

        /* If renderer needs per-model groups: */ 
        const std::unordered_map<ModelId, std::vector<Handle>>& instancesPerModel() const { return instanceData_.instancesPerModel; }

        /* Ctor */
        explicit InstanceManager(VkRenderData& renderData_, EngineDevice& engineDevice_)
            : renderData(renderData_), engineDevice(engineDevice_) {

            {
                /* Null model instance - Utility */
                std::shared_ptr<AvengModel> nullModel = std::make_shared<AvengModel>(engineDevice);
                modelData_.models.emplace_back(nullModel);

                // Note: Manually setting the modelId for the null-instance
                Instance nullInstance = Instance(0, nullModel.get());

                Slot slot = {
                    nullInstance, // instance
                    1, // generation
                    true // alive
                };

                Handle handle = {
                    instanceData_.slots.size(), // index
                    1 // generation
                };

                // NOTE: We probably won't need to keep null instance in instancesPerModel anymore - test later
                instanceData_.instancesPerModel[0].emplace_back(handle);
                instanceData_.slots.emplace_back(slot);
                // assignInstanceIndices();
            }

        }

    private:

        uint32_t acquireSlotIndex() {
            if (!instanceData_.free.empty()) {
                uint32_t idx = instanceData_.free.back();
                instanceData_.free.pop_back();
                return idx;
            }
            uint32_t idx = static_cast<uint32_t>(instanceData_.slots.size());
            instanceData_.slots.emplace_back(); // default Slot: instance=nullopt, alive=false, generation=1
            return idx;
        }

        // Note: Broken
        void updateTriangleCount(unsigned int triangles) {
            // renderData.rdTriangleCount = 0;
            //for (const auto& instanceSlot : data_.miInstanceSlots) {
                renderData.rdTriangleCount += triangles;
            //}
        }

        //// Note: also broken
        //void assignInstanceIndices() {
        //    std::cout << "ASSINGING INDICES:" << std::endl;
        //    for (size_t i = 0; i < instanceData_.instancesInOrder.size(); ++i) {
        //        std::cout
        //            << "modInstanceData.miAssimpInstances["
        //            << i << "] "
        //            << instanceData_.slots[instanceData_.instancesInOrder[i].index].instance.getModel()->getModelFileName()
        //            << std::endl;

        //        InstanceSettings instSettings =
        //            instanceData_.slots[instanceData_.instancesInOrder[i].index]
        //            .instance.getInstanceSettings();

        //        instSettings.isInstanceIndexPosition = i;
        //        data_.miInstanceSlots[data_.miInstancesInOrder[i].index].instance.setInstanceSettings(instSettings);
        //    }
        //}

    public:

        /* get() */
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

        /* TODO - We probably need a new method to achieve this same result [See]: IModelSource.h */
        bool hasModel(const std::string& modelFileName) {
            auto modelIter = std::find_if(modelData_.models.begin(), modelData_.models.end(),
                [modelFileName](const auto& model) {
                return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
            });
            return modelIter != modelData_.models.end();
        }

        /* TODO - We probably need a new method to achieve this same result [See]: IModelSource.h */
        std::shared_ptr<AvengModel> getModel(const std::string& modelFileName) {
            auto modelIter = std::find_if(modelData_.models.begin(), modelData_.models.end(),
                [modelFileName](const auto& model) {
                return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
            });
            if (modelIter != modelData_.models.end()) {
                return *modelIter;
            }
            return nullptr;
        }

        /* */
        void deleteInstance(const Handle& h)
        {
            // --- 1) Validate handle/index/generation/alive ---
            if (h.generation == 0) return;
            if (h.index >= instanceData_.slots.size()) return;

            Slot& slot = instanceData_.slots[h.index];

            if (!slot.alive) return;
            if (slot.generation != h.generation) return;
            if (!slot.instance.has_value()) return; // should be true if alive, but keep it defensive

            // --- 2) Capture model key BEFORE destroying the instance ---
            // (Your Instance stores AvengModel*; this may become nullptr on destruction/reset.)
            Instance& inst = slot.instance.value();
            ModelId mid = inst.modelId();
            auto& vec = instanceData_.instancesPerModel[mid];
            vec.erase(std::remove(vec.begin(), vec.end(), h), vec.end());

            // --- 3) Remove handle from "instancesInOrder" ---
            // O(n), but simple and correct. You can later optimize with a back-pointer index.
            // For now, instances are mostly reused, and only deleted upon cleanup.
            {
                auto& v = instanceData_.instancesInOrder;
                v.erase(std::remove(v.begin(), v.end(), h), v.end());
            }

            // --- 4) Remove handle from "instancesPerModel[modelKey]" ---
            // Only if we have a modelKey (model might be null if something went wrong).
            if (!modelKey.empty()) {
                auto it = instanceData_.instancesPerModel.find(modelKey);
                if (it != instanceData_.instancesPerModel.end()) {
                    auto& vec = it->second;
                    vec.erase(std::remove(vec.begin(), vec.end(), h), vec.end());

                    // Optional cleanup: remove the map entry if now empty
                    if (vec.empty()) {
                        instanceData_.instancesPerModel.erase(it);
                    }
                }
            }

            // --- 5) Destroy the instance in the slot ---
            // This runs Instance's destructor. (If Instance just owns pointers, destructor is trivial.)
            slot.instance.reset();

            // --- 6) Invalidate slot + recycle index ---
            slot.alive = false;

            // Bump generation so old handles become stale.
            // (Be mindful of overflow; see note below.)
            slot.generation = (slot.generation == std::numeric_limits<uint32_t>::max())
                ? 1u
                : slot.generation + 1u;

            instanceData_.free.push_back(h.index);

            // --- 7) Optional: callbacks / bookkeeping ---
            // If you have callbacks per pool:
            // if (callbacks_.onDelete) callbacks_.onDelete(h);

            // assignInstanceIndices()
            // updateTriangleCount()
        }


        void deleteInstances(std::vector<const Handle& h> instances) {
        
        }

        Handle createInstance(const ModelRef& m) {

            assert(m.model != nullptr && m.id != 0 && "Attempting to create instance from invalid modelRef");

            uint32_t slotIndex = acquireSlotIndex(); // side-effects

            InstanceSlot& slot = instanceData_.slots[slotIndex];
            
            slot.instance = Instance(model);
            slot.alive = true;

            // NOTE: generation already bumped on delete
            return InstanceHandle{
                slotIndex,
                slot.generation
            };
        }

        //void addInstance(const ModelId& model) {
        //    createInstance(model.get());

        //    // assignInstanceIndices(); Replaced with slots / handles

        //    // Update triangles in the scene
        //    updateTriangleCount(model->getTriangleCount());
        //}


        void addInstances(std::vector<const ModelId&> h, unsigned int n) {
            //if (model->hasAnimations()) {
            //    size_t animClipNum = model->getAnimClips().size();
            //    for (int i = 0; i < numInstances; ++i) {
            //        int xPos = std::rand() % 50 - 25;
            //        int zPos = std::rand() % 50 - 25;
            //        int rotation = std::rand() % 360 - 180;
            //        int clipNr = std::rand() % animClipNum;

            //        Instance newInstance = Instance(model.get(), glm::vec3(xPos, 0.0f, zPos), glm::vec3(0.0f, rotation, 0.0f));
            //        if (animClipNum > 0) {
            //            InstanceSettings instSettings = newInstance.getInstanceSettings();
            //            instSettings.isAnimClipNr = clipNr;
            //            newInstance.setInstanceSettings(instSettings);
            //        }

            //        InstanceHandle handle{

            //        };

            //        InstanceSlot slot{
            //            newInstance,
            //            1,
            //            true
            //        };

            //        data_.miInstancesInOrder.emplace_back(handle);
            //        data_.miInstanceSlots.emplace_back(slot);
            //        data_.miInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
            //    }
            //}
            //else {

            //    for (int i = 0; i < numInstances; ++i) {
            //        int xPos = std::rand() % 50 - 25;
            //        int zPos = std::rand() % 50 - 25;
            //        int rotation = std::rand() % 360 - 180;

            //        std::shared_ptr<Instance> newInstance = std::make_shared<Instance>(model, glm::vec3(xPos, 0.0f, zPos), glm::vec3(0.0f, rotation, 0.0f));

            //        data_.miAssimpInstances.emplace_back(newInstance);
            //        data_.miAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
            //    }

            //}

            //assignInstanceIndices();
            //updateTriangleCount();
        }

        /*
        * Very tutorial with the get/set of settings
        * Cant this just be combined with cloneInstances?
        * There's no diff between this fn and passing numClones = 1 in cloneInstances
        */
        void cloneInstanceFrom(const Handle& handle, AvengModel* model, const InstanceSettings& settings) {

            uint32_t slotIndex = acquireSlotIndex();
            auto& slot = instanceData_.slots[slotIndex];

            // bump generation on reuse
            if (slot.alive) {
                // should never happen if your free list is correct
                std::cout << "ANOMALY [1]" << std::endl;
            }

            slot.generation = std::max(slot.generation + 1, 1u);
            slot.alive = true;

            slot.instance.emplace(model);          // construct Instance in-place
            slot.instance->setInstanceSettings(settings);

            Handle h{ slotIndex, slot.generation };
  
            instanceData_.instancesInOrder.push_back(h);
            instanceData_.instancesPerModel[model->getModelFileName()].push_back(h); // <-- I recommend storing handles, not copies

            // assignInstanceIndices();
            // updateTriangleCount();
            return h;
        }
        
        void cloneInstances(const Handle& handle, int numClones) {

            auto& src = instanceData_.slots[handle.index].instance.value();
            AvengModel* currentModel = src.getModel();
           
            size_t animClipNum = model->getAnimClips().size();

            for (int i = 0; i < numClones; ++i) {
                InstanceSettings instSettings = instance->getInstanceSettings();
                int xPos = std::rand() % 50 - 25;
                int zPos = std::rand() % 50 - 25;
                int rotation = std::rand() % 360 - 180;

                instSettings.isWorldPosition = glm::vec3(xPos, 0.0f, zPos);
                instSettings.isWorldRotation = glm::vec3(0.0f, rotation, 0.0f);

                if (animClipNum > 0) 
                {

                    int clipNr = std::rand() % animClipNum;
                    float animSpeed = (std::rand() % 50 + 75) / 100.0f;
                    instSettings.isAnimClipNr = clipNr;
                    instSettings.isAnimSpeedFactor = animSpeed;

                }

                cloneOneFrom(handle, model, instSettings);

            }

            // assignInstanceIndices();
            // updateTriangleCount();
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

        InstanceCallbacks   callbacks_{};
        InstanceData        instanceData_{};
        ModelRegistryData   modelData_{};
        VkRenderData& renderData;
        EngineDevice& engineDevice;

    };
}