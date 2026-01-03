#pragma once

#include "avpch.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Editor/Utils/selection_utils.h"

namespace aveng {

    // Forward declarations (adjust includes/types to match your project)
    using ModelId = std::uint32_t;

    // If AssetKey is a typedef/using of std::string, you can forward declare differently.
    // It's usually simplest to include the header that defines AssetKey, ModelRef, etc.
    // For now we forward-declare as a type.

    struct InstanceTransform;
 
    /*
        IEditorUIAPI
        -----------
        A narrow interface the UI layer (AvengImgui) uses to:
        - request model loads/unloads
        - spawn/clone/destroy instances
        - manipulate selection
        - read/write instance transforms for gizmo/inspector
        - list models/instances for panels (outliner, asset browser, etc.)

        Philosophy:
        - UI calls commands; Editor decides policy (e.g. "don’t spawn until loaded").
        - UI can query minimal state needed to render panels.
    */
    struct IEditorUIAPI {
        virtual ~IEditorUIAPI() = default;

        virtual const uint32_t nModels() const = 0;
        virtual bool uiIsModelLoaded(ModelId id, ModelMeta& meta) = 0;

        // Editor-Only Types
        virtual const std::vector<UiModelRow> uiListModels() const = 0;
        virtual const std::vector<UiInstanceRow> uiListInstances() const = 0;
        virtual bool uiTryGetInstance(AnyInstanceHandle h, InstanceView& out) const = 0;
        virtual ModelRef uiGetOrLoadModel(const AssetKey& key) = 0;
        virtual bool     uiUnloadModel(const AssetKey& key) = 0;
        virtual void uiDestroyAllInstancesForModel(ModelId id) = 0;
        virtual AnyInstanceHandle uiSpawn(ModelRef modelRef, const TransformSettings& s) = 0;
        virtual AnyInstanceHandle uiSpawn(ModelRef modelRef, const AnimatedCreateSettings& s) = 0;
        virtual std::vector<AnyInstanceHandle> uiSpawnMany( /// Static
            ModelRef modelRef,
            std::span<const TransformSettings> settings,
            std::uint32_t count) = 0;

        virtual std::vector<AnyInstanceHandle> uiSpawnMany( /// Animated
            ModelRef modelRef,
            std::span<const AnimatedCreateSettings> settings,
            std::uint32_t count) = 0;
        virtual AnyInstanceHandle uiClone(AnyInstanceHandle src) = 0;
        virtual std::vector<AnyInstanceHandle> uiCloneMany(std::span<const AnyInstanceHandle> src) = 0;
        virtual void uiDestroy(AnyInstanceHandle h) = 0;
        virtual void uiDestroyMany(std::span<const AnyInstanceHandle> handles) = 0;
        virtual bool uiTryGetInstanceTransform(AnyInstanceHandle h, InstanceTransform& out) const = 0;
        virtual void uiSetInstanceTransform(AnyInstanceHandle h, const InstanceTransform& t) = 0;

        virtual const std::unordered_map<AssetKey, ModelMeta> uiMapModels() const = 0;
        virtual const std::vector<ModelRef> uiListModelRefs() const = 0;
        virtual const std::vector<AssetKey> uiListModelKeys() const = 0;
        virtual const std::vector<AnyInstanceHandle> uiListInstancesForModel(ModelId id) const = 0;


        // Optional conveniences (useful for model panel UX)
        /// virtual std::optional<ModelId> uiGetSelectedModel() const = 0;
        /// virtual void uiSetSelectedModel(std::optional<ModelId> id) = 0;

        // -----------------------
        // Instance operations
        // -----------------------

        // Spawn many (UI can use this for "scatter", "duplicate N times", etc.)

        // Model-scoped deletes (useful for a model browser)

        /// virtual bool uiPurgeAllInstancesForModel(ModelId id) = 0;

        // -----------------------
        // Selection
        // -----------------------
        /// virtual AnyInstanceHandle uiGetSelectedInstance() const = 0;
        /// virtual void uiSetSelectedInstance(AnyInstanceHandle h) = 0;
        /// virtual void uiClearSelectedInstance() = 0;

        // -----------------------
        // Instance transform (gizmo / inspector)
        // -----------------------
        // Read current transform of an instance. Returns false if handle invalid/dead.

        // Set transform for a single instance.

        // Bulk set (multi-select editing, group transforms, etc.)
        /// TODO
       /* virtual void uiSetInstanceTransforms(
            std::span<const AnyInstanceHandle> handles,
            std::span<const InstanceTransform> transforms) = 0;*/

        // -----------------------
        // Listing for panels
        // -----------------------
        // These are intentionally coarse. UI can request lists and then query details
        // via model/meta systems you already have, or via additional Editor-level helpers.
        // virtual const std::vector<AnyInstanceHandle> uiListInstances() const = 0;

        // -----------------------
        // Optional: message/toast feed for UI
        // -----------------------
        // If you want the UI to show warnings like "Model still loading", "Invalid handle", etc.
        // you can implement this as a ring buffer in EditorData.
        /// TODO virtual void uiPushStatusMessage(const char* msg) = 0;
    };

} // namespace aveng
