#pragma once
#include "Editor/API/IEditorUIAPI.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Editor/Utils/selection_utils.h"

namespace aveng {

    class SceneFacade;

	class SceneEditAPI : public IEditorUIAPI
	{
    public:
        SceneEditAPI(SceneFacade& scene);

        const uint32_t nModels() const override;

        const std::vector<UiModelRow> uiListModels() const override;
        const std::vector<UiInstanceRow> uiListInstances() const override;
        bool uiTryGetInstance(AnyInstanceHandle h, InstanceView& out) const override;
        ModelRef uiGetOrLoadModel(const AssetKey& key) override;
        bool     uiUnloadModel(const AssetKey& key) override;
        void uiDestroyAllInstancesForModel(ModelId id) override;
        AnyInstanceHandle uiSpawn(ModelRef modelRef, const TransformSettings& s) override;
        AnyInstanceHandle uiSpawn(ModelRef modelRef, const AnimatedCreateSettings& s) override;
        std::vector<AnyInstanceHandle> uiSpawnMany( /// Static
            ModelRef modelRef,
            std::span<const TransformSettings> settings,
            std::uint32_t count) override;
        std::vector<AnyInstanceHandle> uiSpawnMany( /// Animated
            ModelRef modelRef,
            std::span<const AnimatedCreateSettings> settings,
            std::uint32_t count) override;
        AnyInstanceHandle uiClone(AnyInstanceHandle src) override;
        std::vector<AnyInstanceHandle> uiCloneMany(std::span<const AnyInstanceHandle> src) override;
        void uiDestroy(AnyInstanceHandle h) override;
        void uiDestroyMany(std::span<const AnyInstanceHandle> handles) override;
        bool uiTryGetInstanceTransform(AnyInstanceHandle h, InstanceTransform& out) const override;
        void uiSetInstanceTransform(AnyInstanceHandle h, const InstanceTransform& t) override;

        const std::unordered_map<AssetKey, ModelMeta> uiMapModels() const override;
        const std::vector<ModelRef> uiListModelRefs() const override;
        const std::vector<AssetKey> uiListModelKeys() const override;
        const std::vector<AnyInstanceHandle> uiListInstancesForModel(ModelId id) const override;

        // Selection - Model & Instance
        /// TODO std::optional<ModelId> uiGetSelectedModel() const override;
        /// TODO void uiSetSelectedModel(std::optional<ModelId> id) override;
        /// TODO AnyInstanceHandle uiGetSelectedInstance() const override;
        /// TODO void uiSetSelectedInstance(AnyInstanceHandle h) override;
        /// TODO void uiClearSelectedInstance() override;
        /* Instance operations */



        // -----------------------
        // Instance transform (gizmo / inspector)
        // -----------------------
        // Read current transform of an instance. Returns false if handle invalid/dead.

        // Set transform for a single instance.

        // Bulk set (multi-select editing, group transforms, etc.)
        /// TODO
        //void uiSetInstanceTransforms(
        //    std::span<const AnyInstanceHandle> handles,
        //    std::span<const InstanceTransform> transforms) override;

        // -----------------------
        // Listing for panels
        // -----------------------
        // These are intentionally coarse. UI can request lists and then query details
        // via model/meta systems you already have, or via additional Editor-level helpers.
        // const std::vector<AnyInstanceHandle> uiListInstances() const override;

        // -----------------------q
        // Optional: message/toast feed for UI
        // -----------------------
        // If you want the UI to show warnings like "Model still loading", "Invalid handle", etc.
        // you can implement this as a ring buffer in EditorData.
        /// TODO void uiPushStatusMessage(const char* msg) override;

    private:
        SceneFacade& scene_;
	};

}