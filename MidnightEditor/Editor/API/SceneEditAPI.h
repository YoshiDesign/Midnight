#pragma once
#include "Editor/API/IEditorUIAPI.h"
#include "Core/Modeling/ModelAndInstanceData.h"

namespace aveng {

    class SceneFacade;

	class SceneEditAPI : public IEditorUIAPI
	{
    public:

        SceneEditAPI(SceneFacade& scene);
	
        /* Model Operations */
        virtual ModelRef uiGetOrLoadModel(const AssetKey& key) override;
        virtual bool     uiUnloadModel(const AssetKey& key) override;
        virtual void uiDestroyAllInstancesForModel(ModelId id) override;
        /// Prolly not TODO virtual bool uiPurgeAllInstancesForModel(ModelId id) override;
        // Selection - Model & Instance
        /// TODO virtual std::optional<ModelId> uiGetSelectedModel() const override;
        /// TODO virtual void uiSetSelectedModel(std::optional<ModelId> id) override;
        /// TODO virtual AnyInstanceHandle uiGetSelectedInstance() const override;
        /// TODO virtual void uiSetSelectedInstance(AnyInstanceHandle h) override;
        /// TODO virtual void uiClearSelectedInstance() override;
        /* Instance operations */
        virtual AnyInstanceHandle uiSpawn(ModelRef modelRef, const TransformSettings& s) override;
        virtual AnyInstanceHandle uiSpawn(ModelRef modelRef, const AnimatedCreateSettings& s) override;
        virtual std::vector<AnyInstanceHandle> uiSpawnMany( /// Static
            ModelRef modelRef,
            std::span<const TransformSettings> settings,
            std::uint32_t count) override;
        virtual std::vector<AnyInstanceHandle> uiSpawnMany( /// Animated
            ModelRef modelRef,
            std::span<const AnimatedCreateSettings> settings,
            std::uint32_t count) override;
        virtual AnyInstanceHandle uiClone(AnyInstanceHandle src) override;
        virtual std::vector<AnyInstanceHandle> uiCloneMany(std::span<const AnyInstanceHandle> src) override;
        virtual void uiDestroy(AnyInstanceHandle h) override;
        virtual void uiDestroyMany(std::span<const AnyInstanceHandle> handles) override;

        // -----------------------
        // Instance transform (gizmo / inspector)
        // -----------------------
        // Read current transform of an instance. Returns false if handle invalid/dead.
        /// TODO virtual bool uiTryGetInstanceTransform(AnyInstanceHandle h, InstanceTransform& out) const override;

        // Set transform for a single instance.
        /// TODO virtual void uiSetInstanceTransform(AnyInstanceHandle h, const InstanceTransform& t) override;

        // Bulk set (multi-select editing, group transforms, etc.)
        /// TODO
        //virtual void uiSetInstanceTransforms(
        //    std::span<const AnyInstanceHandle> handles,
        //    std::span<const InstanceTransform> transforms) override;

        // -----------------------
        // Listing for panels
        // -----------------------
        // These are intentionally coarse. UI can request lists and then query details
        // via model/meta systems you already have, or via additional Editor-level helpers.
        const virtual std::unordered_map<AssetKey, ModelId> uiMapModels() const override;
        const virtual std::vector<ModelRef> uiListModelRefs() const override;
        const std::vector<AssetKey> uiListModelKeys() const override;
        const virtual std::vector<AnyInstanceHandle> uiListInstances() const override;
        const virtual std::vector<AnyInstanceHandle> uiListInstancesForModel(ModelId id) const override;

        // -----------------------q
        // Optional: message/toast feed for UI
        // -----------------------
        // If you want the UI to show warnings like "Model still loading", "Invalid handle", etc.
        // you can implement this as a ring buffer in EditorData.
        /// TODO virtual void uiPushStatusMessage(const char* msg) override;

    private:
        SceneFacade& scene_;
	};

}