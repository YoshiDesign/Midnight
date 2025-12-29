#pragma once
#include "avpch.h"
#include "Core/aveng_model.h"
#include "Core/Modeling/ModelRegistry.h"
#include "Core/Modeling/Sources/FilesystemModelSource.h"

namespace aveng {

    class EngineDevice;

    class ModelLibrary final : public IModelLibrary {
    public:

        ModelLibrary(EngineDevice& engineDevice, VkRenderData& renderData);

        ModelRef getOrLoadModel(const AssetKey& key) override;
        bool unloadModel(const AssetKey& key) override;

        std::unique_ptr<IModelSource> createModelSource();
        std::unique_ptr<AvengModel> buildModelFromSource(const AssetKey& key, std::span<const std::byte> bytes);
        std::string baseDirForAssetKey(const AssetKey& key) const;
        void processPendingModelLoads();

        inline ModelRef makeModelRef(const ModelEntry& e)
        {
            return ModelRef{
                e.id,
                e.isAnimated
            };
        }

        // ---- Query accessors ----
        const IModelQuery& query() const noexcept {
            return registry_;
        }

        const IModelAnimQuery& animQuery() const noexcept {
            return registry_;
        }

        void cleanup();

    private:
        EngineDevice& engineDevice_;
        ModelRegistryData registry_;
        ModelId nextModelId_ = 1; // 0 reserved for the NullModelId

        VkRenderData& renderData_;

        // ModelRegistryData modelDb_;
        std::unique_ptr<IModelSource> modelSource_;

        std::string contentRoot_ = "Assets"; // or "." or absolute
        std::string textureRoot_ = "textures"; // relative to contentRoot_
    };

}