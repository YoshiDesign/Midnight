#pragma once
#include "avpch.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Core/Modeling/InstanceSettings.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"
#include "Core/aveng_model.h"

namespace aveng {

    class AvengModel;
    class EngineDevice;

    template<class Tag>
    class InstanceManager {
    public:
        using Data = ModelAndInstanceData<Tag>;
        using Handle = InstanceHandle<Tag>;
        using Slot = InstanceSlot<Tag>;

        Data& data() { return data_; }
        const Data& data() const { return data_; }

        explicit InstanceManager(VkRenderData& renderData_, EngineDevice& engineDevice_)
            : renderData(renderData_), engineDevice(engineDevice_) {

            /* register callbacks */
            cb.miModelCheckCallbackFunction = [this](const std::string& fileName) { return hasModel(fileName); };
            cb.miModelAddCallbackFunction = [this](const std::string& fileName) {/* return addModel(fileName);*/ return queueModelLoad(fileName); };
            cb.miModelDeleteCallbackFunction = [this](const std::string& modelName) { deleteModel(modelName); };
            cb.miInstanceAddCallbackFunction = [this](std::shared_ptr<AvengModel> model) { return addInstance(model); };
            cb.miInstanceAddManyCallbackFunction = [this](std::shared_ptr<AvengModel> model, int numInstances) { addInstances(model, numInstances); };
            cb.miInstanceDeleteCallbackFunction = [this](const Handle& handle) { deleteInstance(handle); };
            cb.miInstanceCloneCallbackFunction = [this](const Handle& handle) { cloneInstance(handle); };
            cb.miInstanceCloneManyCallbackFunction = [this](const Handle& handle, int numClones) { cloneInstances(handle, numClones); };

            {
                /* Null model instance - Utility */
                std::shared_ptr<AvengModel> nullModel = std::make_shared<AvengModel>(engineDevice);
                data.miModelList.emplace_back(nullModel);

                AssimpInstance nullInstance = AssimpInstance(nullModel.get());

                Slot slot = {
                    nullInstance, // instance
                    1, // generation
                    true // alive
                };

                Handle handle = {
                    data.miInstanceSlots.size(), // index
                    1 // generation
                };

                data.miInstancesPerModel[nullModel->getModelFileName()].emplace_back(handle);
                data.miInstanceSlots.emplace_back(slot);
                assignInstanceIndices();
            }

        }

    private:

        void updateTriangleCount(unsigned int triangles) {
            // renderData.rdTriangleCount = 0;
            //for (const auto& instanceSlot : data.miInstanceSlots) {
                renderData.rdTriangleCount += triangles;
            //}
        }

        void assignInstanceIndices() {
            std::cout << "ASSINGING INDICES:" << std::endl;
            for (size_t i = 0; i < data.miInstancesInOrder.size(); ++i) {
                std::cout
                    << "modInstanceData.miAssimpInstances["
                    << i << "] "
                    << data.miInstanceSlots[data.miInstancesInOrder[i].index].instance.getModel()->getModelFileName()
                    << std::endl;

                InstanceSettings instSettings =
                    data.miInstanceSlots[data.miInstancesInOrder[i].index]
                    .instance.getInstanceSettings();

                instSettings.isInstanceIndexPosition = i;
                data.miInstanceSlots[data.miInstancesInOrder[i].index].instance.setInstanceSettings(instSettings);
            }
        }

    public:

        /* */
        template<class Tag>
        AssimpInstance* get(Handle h) {
            if (h.generation == 0) return nullptr;
            if (h.index >= data.miInstanceSlots.size()) return nullptr;

            auto& slot = data.miInstanceSlots[h.index];
            if (!slot.alive) return nullptr;
            if (slot.generation != h.generation) return nullptr;

            return &slot.instance;
        }

        /* */
        template<class Tag>
        const AssimpInstance* get(Handle h) const {
            return const_cast<InstanceManager*>(this)->get(h);
        }

        /* */
        bool hasModel(const std::string& modelFileName) {
            auto modelIter = std::find_if(data.miModelList.begin(), data.miModelList.end(),
                [modelFileName](const auto& model) {
                return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
            });
            return modelIter != data.miModelList.end();
        }

        /* */
        std::shared_ptr<AvengModel> getModel(const std::string& modelFileName) {
            auto modelIter = std::find_if(data.miModelList.begin(), data.miModelList.end(),
                [modelFileName](const auto& model) {
                return model->getModelFileNamePath() == modelFileName || model->getModelFileName() == modelFileName;
            });
            if (modelIter != data.miModelList.end()) {
                return *modelIter;
            }
            return nullptr;
        }

        /* */
        const std::vector<Handle>& instancesInOrder() const { return data.miInstancesInOrder; }
        /* */
        const std::vector<Slot>& slots() const { return data.miInstanceSlots; }

        // If renderer needs per-model groups:
        const auto& instancesPerModel() const { return data.miInstancesPerModel; }

        // Instance pool mutations
        void deleteInstance(Handle h) {
            ////std::shared_ptr<AvengModel> currentModel = instance->getModel();
            //AvengModel* currentModel = instance->getModel();
            //std::string currentModelName = currentModel->getModelFileName();

            //data.miAssimpInstances.erase(
            //    std::remove_if(
            //        data.miAssimpInstances.begin(),
            //        data.miAssimpInstances.end(),
            //        [instance](std::shared_ptr<AssimpInstance> inst) {
            //    return inst == instance;
            //}
            //    ));


            //data.miAssimpInstancesPerModel[currentModelName].erase(
            //    std::remove_if(
            //        data.miAssimpInstancesPerModel[currentModelName].begin(),
            //        data.miAssimpInstancesPerModel[currentModelName].end(),
            //        [instance](std::shared_ptr<AssimpInstance> inst) {
            //    return inst == instance;
            //}
            //    ));

            //assignInstanceIndices();
            //updateTriangleCount();
        }

        Handle createInstance(AvengModel* model) {
            uint32_t slotIndex;

            if (!data.miFreeIndices.empty()) {
                // Reuse slot
                slotIndex = data.miFreeIndices.back();
                data.miFreeIndices.pop_back();
            }
            else {
                // Create new slot
                slotIndex = static_cast<uint32_t>(data.miInstanceSlots.size());
                data.miInstanceSlots.emplace_back();
            }

            InstanceSlot& slot = data.miInstanceSlots[slotIndex];

            slot.instance = AssimpInstance(model);
            slot.alive = true;

            // NOTE: generation already bumped on delete
            return InstanceHandle{
                slotIndex,
                slot.generation
            };
        }

        bool addModel(const std::string& modelFileName) {

            // TODO - relocate to the caller
            //if (isFrameStarted) {
            //	std::printf("ERROR: addModel called mid-frame!\n");
            //	throw std::runtime_error("Internal error: model loading during frame");
            //}

            if (hasModel(modelFileName)) {
                std::printf("%s warning: model '%s' already existed, skipping\n", __FUNCTION__, modelFileName.c_str());
                return false;
            }

            std::shared_ptr<AvengModel> model = std::make_shared<AvengModel>(engineDevice);
            if (!model->loadModelV2(renderData, modelFileName)) {
                std::printf("%s error: could not load model file '%s'\n", __FUNCTION__, modelFileName.c_str());
                return false;
            }

            data.miModelList.emplace_back(model);
            addInstance(model);

            return true;
        }

        void deleteModel(const std::string& modelFileName) {
            std::string shortModelFileName = std::filesystem::path(modelFileName).filename().generic_string();

            if (!data.miInstanceSlots.empty()) {
                data.miInstanceSlots.erase(
                    std::remove_if(
                        data.miInstanceSlots.begin(),
                        data.miInstanceSlots.end(),
                        [shortModelFileName](std::shared_ptr<AssimpInstance> instance) {
                    return instance->getModel()->getModelFileName() == shortModelFileName;
                }
                    ),
                    data.miInstanceSlots.end()
                );
            }

            // Delete every model in the hash, and delete the hash
            if (data.miInstancesPerModel.count(shortModelFileName) > 0) {
                data.miInstancesPerModel[shortModelFileName].clear();
                data.miInstancesPerModel.erase(shortModelFileName);
            }

            /* add models to pending delete list */
            for (const auto& model : data.miModelList) {
                if (model && (model->getTriangleCount() > 0)) {
                    data.miPendingDeleteAvengModels.insert(model);
                }
            }

            data.miModelList.erase(
                std::remove_if(
                    data.miModelList.begin(),
                    data.miModelList.end(),
                    [modelFileName](std::shared_ptr<AvengModel> model) {
                return model->getModelFileName() == modelFileName;
            }
                )
            );

            updateTriangleCount();
        }

        // Riddle me this - To load a model, we don't necessarily care what type it is, right?
        // That must be resolved after it's loaded
        bool queueModelLoad(const std::string& filepath) {
            data.mPendingModelLoads.push_back(filepath);
            std::printf("Queued model load (will load after current frame)\n");
            return true;
        }

        void processPendingModelLoads() {
            if (data.mPendingModelLoads.empty()) {
                return;
            }

            for (const auto& filepath : data.mPendingModelLoads) {
                if (hasModel(filepath))
                {
                    return;
                }

                std::printf("Processing queued model load: %s\n", filepath.c_str());
                // if (!addModel(pending.filepath)) {
                if (!addModel(filepath)) {
                    std::printf("Failed to load queued model: %s\n", filepath.c_str());
                }
                else {
                    /* select new model and new instance */
                    data.miSelectedEditorModel = data.miModelList.size() - 1;

                    // TODO
                    data.miSelectedEditorInstance = data.miInstancesInOrder.size() - 1; // data.miAssimpInstances.size() - 1;
                }
            }

            data.mPendingModelLoads.clear();
        }


        void addInstance(std::shared_ptr<AvengModel> model) {
            createInstance(model.get());

            // assignInstanceIndices(); Replaced with slots / handles

            // Update triangles in the scene
            updateTriangleCount(model->getTriangleCount());
        }


        void addInstances(std::shared_ptr<AvengModel> model, int numInstances) {
            //if (model->hasAnimations()) {
            //    size_t animClipNum = model->getAnimClips().size();
            //    for (int i = 0; i < numInstances; ++i) {
            //        int xPos = std::rand() % 50 - 25;
            //        int zPos = std::rand() % 50 - 25;
            //        int rotation = std::rand() % 360 - 180;
            //        int clipNr = std::rand() % animClipNum;

            //        AssimpInstance newInstance = AssimpInstance(model.get(), glm::vec3(xPos, 0.0f, zPos), glm::vec3(0.0f, rotation, 0.0f));
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

            //        data.miInstancesInOrder.emplace_back(handle);
            //        data.miInstanceSlots.emplace_back(slot);
            //        data.miInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
            //    }
            //}
            //else {

            //    for (int i = 0; i < numInstances; ++i) {
            //        int xPos = std::rand() % 50 - 25;
            //        int zPos = std::rand() % 50 - 25;
            //        int rotation = std::rand() % 360 - 180;

            //        std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model, glm::vec3(xPos, 0.0f, zPos), glm::vec3(0.0f, rotation, 0.0f));

            //        data.miAssimpInstances.emplace_back(newInstance);
            //        data.miAssimpInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
            //    }

            //}

            //assignInstanceIndices();
            //updateTriangleCount();
        }
        void cloneInstance(const Handle& handle) {
            //std::shared_ptr<AvengModel> currentModel = instance->getModel();
            std::shared_ptr<AvengModel>currentModel = data.miInstanceSlots[instanceHandle.index].instance.getModel();
            AssimpInstance newInstance = AssimpInstance{ currentModel };
            InstanceSettings newInstanceSettings = data.miInstanceSlots[instanceHandle.index].instance.getInstanceSettings();

            /* slight offset to see new instance */
            newInstanceSettings.isWorldPosition += glm::vec3(1.0f, 0.0f, -1.0f);
            newInstance.setInstanceSettings(newInstanceSettings);

            InstanceHandle handle{
                data.miInstanceSlots.size() - 1,
                1
            };

            data.miInstanceSlots.emplace_back(InstanceSlot{ newInstance, 1, true });
            data.miInstancesInOrder.emplace_back(handle);
            data.miInstancesPerModel[currentModel->getModelFileName()].emplace_back(handle);

            assignInstanceIndices();
            updateTriangleCount();
        }
        void cloneInstances(const Handle&, int numClones) {

            std::shared_ptr<AvengModel> model = instance->getModel();
            AvengModel* model = instance.getModel();
            size_t animClipNum = model->getAnimClips().size();
            for (int i = 0; i < numClones; ++i) {
                int xPos = std::rand() % 50 - 25;
                int zPos = std::rand() % 50 - 25;
                int rotation = std::rand() % 360 - 180;

                int clipNr = std::rand() % animClipNum;
                float animSpeed = (std::rand() % 50 + 75) / 100.0f;

                //std::shared_ptr<AssimpInstance> newInstance = std::make_shared<AssimpInstance>(model);
                AssimpInstance newInstance = AssimpInstance{ model };
                InstanceSettings instSettings = instance->getInstanceSettings();
                instSettings.isWorldPosition = glm::vec3(xPos, 0.0f, zPos);
                instSettings.isWorldRotation = glm::vec3(0.0f, rotation, 0.0f);
                if (animClipNum > 0) {
                    instSettings.isAnimClipNr = clipNr;
                    instSettings.isAnimSpeedFactor = animSpeed;
                }

                newInstance->setInstanceSettings(instSettings);

                data.miInstanceSlots.emplace_back(newInstanceHandle);
                data.miInstancesPerModel[model->getModelFileName()].emplace_back(newInstance);
            }

            assignInstanceIndices();
            updateTriangleCount();
        }

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

        const std::vector<Handle>& instancesInOrder() const { return data.miInstancesInOrder; }

    private:
        Data data;
        ModelAndInstanceCallbacks& cb;
        VkRenderData& renderData;
        EngineDevice& engineDevice;
    };
}