// SceneFacade.cpp
#include "SceneFacade.h"

#include <cassert>
#include <utility>      // std::move
#include <variant>      // std::visit
#include <type_traits>  // std::is_same_v

// NOTE:
// This .cpp assumes IModelQuery exposes something like:
//   bool isModelLoaded(ModelId) const;
//   bool isAnimated(ModelId) const;          // or isModelAnimated(...)
// If your actual names differ, just rename the calls in validateModel().
//
// It also assumes InstanceManager exposes something like:
//   Handle create(ModelId, const InstanceSettings&);
//   Handle clone(Handle);
//   void   destroy(Handle);
//   void   destroyMany(std::span<const Handle>);   // optional, we don't require it
//   void   setTransform(Handle, const Transform&); // or setTransform(Handle, span<Transform>) etc.
// Adjust those few call sites to match your concrete API.
namespace aveng {
    SceneFacade::SceneFacade(IModelLibrary& modelLib, const IModelQuery& modelQuery)
        : modelLib_(modelLib)
        , modelQuery_(modelQuery)
    {
    }

    /* IModelLibrary */
    ModelRef SceneFacade::getOrLoadModel(const AssetKey& key)
    {
        // Delegate to the real library.
        // ModelRef is assumed to contain at least .id
        const ModelRef ref = modelLib_.getOrLoadModel(key);

        // Per your note: ignore validated_ cache and rely on modelQuery_.isModelLoaded()
        // (You can remove validated_ from the header later if desired.)
        return ref;
    }

    bool SceneFacade::unloadModel(const AssetKey& key)
    {
        const bool ok = modelLib_.unloadModel(key);

        // We only have the key here, and SceneFacade has no key->ModelId mapping in this header.
        // So we can’t precisely invalidate a single ModelId unless IModelQuery supports lookup by key.
        //
        // Safe conservative choice: clear cached validation (even though we’re not relying on it).
        clearValidationCache();

        return ok;
    }

    /* Instance Ops */
    /* Unused at the moment! Since we can easily query via modelQuery_ */
    std::optional<SceneFacade::ModelValidation> SceneFacade::validateModel(ModelId id) const
    {
        // Ignore validated_ map per your request.
        // We treat modelQuery_ as the single source of truth.
        ModelMeta m{};
        if (!modelQuery_.isModelLoaded(id, m)) {
            return std::nullopt;
        }

        ModelValidation v{};
        v.pool = m.animated ? PoolKind::Animated : PoolKind::Static;
        return v;
    }

    AnyInstanceHandle SceneFacade::spawn(ModelRef modelRef, const TransformSettings& s)
    {
        // This as a validation isn't very appealing
        // I should be checking for if (ModelId == NullModelId) ...slow n steady
        if (!modelRef) {
            if (cfg_.failSoft) return AnyInstanceHandle{};
            assert(false && "SceneFacade::spawn: null ModelRef");
            return AnyInstanceHandle{};
        }

        // Validate modelId is actually loaded (and get authoritative meta if you want).
        ModelMeta m{};
        if (!modelQuery_.isModelLoaded(modelRef.id, m)) {
            if (cfg_.failSoft) return AnyInstanceHandle{};
            assert(false && "SceneFacade::spawn: model id not loaded");
            return AnyInstanceHandle{};
        }

        // Prefer receipt (ModelRef) for intent; meta is authority if you want to sanity-check.
        const PoolKind pool = modelRef.isAnimated ? PoolKind::Animated : PoolKind::Static;

        // Optional: sanity check in debug builds (helps catch stale/corrupt ModelRef).
#ifndef NDEBUG
        if (modelRef.isAnimated != m.animated) {
            // If this ever fires, it means ModelRef receipt is inconsistent with registry.
            assert(false && "SceneFacade::spawn: ModelRef.isAnimated disagrees with ModelMeta.animated");
        }
#endif

        return spawnValidated(pool, modelRef.id, s, m);
    }

    AnyInstanceHandle SceneFacade::spawn(ModelRef modelRef, const AnimatedCreateSettings& s)
    {
        // This as a validation isn't very appealing
        // I should be checking for if (ModelId == NullModelId) ...slow n steady
        if (!modelRef) {
            if (cfg_.failSoft) return AnyInstanceHandle{};
            assert(false && "SceneFacade::spawn: null ModelRef");
            return AnyInstanceHandle{};
        }

        // Validate modelId is actually loaded (and get authoritative meta if you want).
        ModelMeta m{};
        if (!modelQuery_.isModelLoaded(modelRef.id, m)) {
            if (cfg_.failSoft) return AnyInstanceHandle{};
            assert(false && "SceneFacade::spawn: model id not loaded");
            return AnyInstanceHandle{};
        }

        // Prefer receipt (ModelRef) for intent; meta is authority if you want to sanity-check.
        const PoolKind pool = modelRef.isAnimated ? PoolKind::Animated : PoolKind::Static;

        // Optional: sanity check in debug builds (helps catch stale/corrupt ModelRef).
#ifndef NDEBUG
        if (modelRef.isAnimated != m.animated) {
            // If this ever fires, it means ModelRef receipt is inconsistent with registry.
            assert(false && "SceneFacade::spawn: ModelRef.isAnimated disagrees with ModelMeta.animated");
        }
#endif

        return spawnValidated(pool, modelRef.id, s, m);
    }


    std::vector<AnyInstanceHandle> SceneFacade::spawnMany(
        ModelRef modelRef,
        std::span<const InstanceSettings> settings,
        std::uint32_t count)
    {
        std::vector<AnyInstanceHandle> out;
        out.reserve(count);

        if (count == 0) return out;

        if (!modelRef) {
            if (!cfg_.failSoft) assert(false && "SceneFacade::spawnMany: null ModelRef");
            return out;
        }

        if (settings.empty()) {
            if (!cfg_.failSoft) assert(false && "SceneFacade::spawnMany: settings span empty");
            return out;
        }

        ModelMeta m{};
        if (!modelQuery_.isModelLoaded(modelRef.id, m)) {
            if (!cfg_.failSoft) assert(false && "SceneFacade::spawnMany: model id not loaded");
            return out;
        }

        const PoolKind pool = modelRef.isAnimated ? PoolKind::Animated : PoolKind::Static;

#ifndef NDEBUG
        if (modelRef.isAnimated != m.animated) {
            assert(false && "SceneFacade::spawnMany: ModelRef.isAnimated disagrees with ModelMeta.animated");
        }
#endif

        for (std::uint32_t i = 0; i < count; ++i) {
            const InstanceSettings& s = settings[i % settings.size()];
            out.emplace_back(spawnValidated(pool, modelRef.id, s, m));
        }

        return out;
    }


    AnyInstanceHandle SceneFacade::spawnValidated(PoolKind pool, ModelId id, const InstanceSettings& settings, const ModelMeta& m)
    {
        switch (pool) {
        case PoolKind::Static: {
            // Adjust create(...) signature to your InstanceManager API.
            const StaticHandle h = staticMgr_.addInstanceOfModel(id, settings, m);
            return AnyInstanceHandle{ h };
        }
        case PoolKind::Animated: {
            const AnimatedHandle h = animatedMgr_.addInstanceOfModel(id, settings, m);
            return AnyInstanceHandle{ h };
        }
        default:
            break;
        }

        if (cfg_.failSoft) return AnyInstanceHandle{};
        assert(false && "SceneFacade::spawnValidated: unknown pool kind");
        return AnyInstanceHandle{};
    }

    AnyInstanceHandle SceneFacade::clone(AnyInstanceHandle src)
    {
        return std::visit([&](auto&& h) -> AnyInstanceHandle {
            using H = std::decay_t<decltype(h)>;

            if constexpr (std::is_same_v<H, std::monostate>) {
                return AnyInstanceHandle{};
            }
            else if constexpr (std::is_same_v<H, StaticHandle>) {
                const StaticHandle nh = staticMgr_.cloneInstance(h);
                return AnyInstanceHandle{ nh };
            }
            else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                const AnimatedHandle nh = animatedMgr_.cloneInstance(h);
                return AnyInstanceHandle{ nh };
            }
            else {
                // If you later add more handle types, you'll land here until you update.
                if (cfg_.failSoft) return AnyInstanceHandle{};
                assert(false && "SceneFacade::clone: unhandled handle variant alternative");
                return AnyInstanceHandle{};
            }
        }, src);
    }

    std::vector<AnyInstanceHandle> SceneFacade::cloneMany(std::span<const AnyInstanceHandle> srcHandles)
    {
        std::vector<AnyInstanceHandle> out;
        out.reserve(srcHandles.size());

        for (const auto& h : srcHandles) {
            out.emplace_back(clone(h));
        }
        return out;
    }

    void SceneFacade::destroyStatic(StaticHandle h)
    {
        staticMgr_.deleteInstance(h);
    }

    void SceneFacade::destroyAnimated(AnimatedHandle h)
    {
        animatedMgr_.deleteInstance(h);
    }

    void SceneFacade::destroy(AnyInstanceHandle h)
    {
        std::visit([&](auto&& hh) {
            using H = std::decay_t<decltype(hh)>;

            if constexpr (std::is_same_v<H, std::monostate>) {
                // no-op
            }
            else if constexpr (std::is_same_v<H, StaticHandle>) {
                destroyStatic(hh);
            }
            else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                destroyAnimated(hh);
            }
            else {
                if (!cfg_.failSoft) {
                    assert(false && "SceneFacade::destroy: unhandled handle alternative");
                }
            }
        }, h);
    }

    void SceneFacade::destroyMany(std::span<const AnyInstanceHandle> handles)
    {
        // Simple + safe implementation: dispatch per element.
        // You can later optimize by batching per pool if InstanceManager exposes destroyMany().
        for (const auto& h : handles) {
            destroy(h);
        }
    }

    /* Transform Ops */

    void SceneFacade::setTransform(AnyInstanceHandle handle, const Transform& transform)
    {

        std::visit([&](auto&& h) {
            using H = std::decay_t<decltype(h)>;

            if constexpr (std::is_same_v<H, std::monostate>) {
                // no-op
            }
            else if constexpr (std::is_same_v<H, StaticHandle>) {
                staticMgr_.setTransform(h, transform);
            }
            else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                animatedMgr_.setTransform(h, transform);
            }
            else {
                if (!cfg_.failSoft) {
                    assert(false && "SceneFacade::setTransform: unhandled handle alternative");
                }
            }
        }, handle);
    }

    void SceneFacade::setTransforms(
        std::span<const AnyInstanceHandle> handles,
        std::span<const Transform> transforms)
    {
        if (handles.size() != transforms.size()) {
            if (cfg_.failSoft) return;
            assert(false && "SceneFacade::setTransforms: handles/transforms size mismatch");
            return;
        }

        for (std::size_t i = 0; i < handles.size(); ++i) {
            const AnyInstanceHandle& h = handles[i];
            const Transform& t = transforms[i];

            std::visit([&](auto&& hh) {
                using H = std::decay_t<decltype(hh)>;

                if constexpr (std::is_same_v<H, std::monostate>) {
                    // no-op
                }
                else if constexpr (std::is_same_v<H, StaticHandle>) {
                    staticMgr_.setTransform(hh, t);
                }
                else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                    animatedMgr_.setTransform(hh, t);
                }
                else {
                    if (!cfg_.failSoft) {
                        assert(false && "SceneFacade::setTransforms: unhandled handle alternative");
                    }
                }
            }, h);
        }
    }
}