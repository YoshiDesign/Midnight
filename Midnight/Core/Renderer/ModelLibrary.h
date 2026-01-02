#pragma once
#include "avpch.h"
#include "Core/aveng_model.h"
#include "Core/Modeling/ModelRegistry.h"
#include "Core/Modeling/Sources/FilesystemModelSource.h"
#include "Core/Modeling/Sources/PackModelSource.h"

namespace aveng {

    class EngineDevice;

    /* 
     *  Important to note that if a model is queued for load AND unload 
     *  simultaneously there could be UB. We'll sort this out soon enough.
     * 
     *  This class's `ModelRegistryData registry_` member overrides these virtual classes:
     *  - IModelQuery
     *  - IModelAnimQuery
     * 
     *  This allows us to expose them as an API via query() and animQuery() 
     *  which get injected into other classes.
     * 
     *  The `onDestroyInstancesForModel_` callback gets registered when we init Midnight
     */

    class ModelLibrary final : public IModelLibrary {
    public:

        using DestroyInstancesForModelFn = std::function<void(ModelId)>;

        void setDestroyInstancesForModelCallback(DestroyInstancesForModelFn fn) {
            onDestroyInstancesForModel_ = std::move(fn);
        }

        ModelLibrary(EngineDevice& engineDevice, VkRenderData& renderData);
        ~ModelLibrary() = default;

        ModelRef getOrLoadModel(const AssetKey& assetKey) override;
        bool unloadModel(const AssetKey& assetKey) override;
        const AvengModel* pModel(ModelId id) const override;

        std::unique_ptr<IModelSource> createModelSource();
        std::unique_ptr<AvengModel> buildModelFromSource(const AssetKey& key, std::span<const std::byte> bytes);
        std::string baseDirForAssetKey(const AssetKey& key) const;
        void processPendingModelLoads();
        void processPendingUnloads();

        // ---- Query accessors ----
        const IModelQuery& query() const noexcept {
            return registry_;
        }

        const IModelAnimQuery& animQuery() const noexcept {
            return registry_;
        }

        void cleanup();

    private:
        ModelRegistryData registry_;
        EngineDevice& engineDevice_;
        VkRenderData& renderData_;
        ModelId nextModelId_ = 1; // 0 reserved for the NullModelId
        DestroyInstancesForModelFn onDestroyInstancesForModel_;
        std::unique_ptr<IModelSource> modelSource_;

        std::string contentRoot_ = "";    // or "." or absolute
        std::string textureRoot_ = "";  // relative to contentRoot_

        /* Note: Does not preserve insertion order */
        void ejectModel(ModelId id, AssetKey key);
    };

}