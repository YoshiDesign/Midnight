#pragma once

#include "Core/Modeling/ModelRegistry.h"
namespace aveng {
    class ModelLibrary final : public IModelLibrary {
    public:

        ModelLibrary() {
            // Create the Null model
        }

        ModelId getOrLoadModel(const AssetKey& key) override;
        bool unloadModel(const AssetKey& key) override;

        // ---- Query accessors ----
        const IModelQuery& query() const noexcept {
            return registry_;
        }

        const IModelAnimQuery& animQuery() const noexcept {
            return registry_;
        }

    private:
        ModelRegistryData registry_;
    };
}