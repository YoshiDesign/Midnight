// SceneFacade.cpp
#include "SceneFacade.h"
#include "Core/aveng_model.h"
#include <cassert>
#include <utility>      // std::move
#include <variant>      // std::visit
#include <type_traits>  // std::is_same_v

namespace {

    /* Helpers for this TU */

    template<class Tag>
    bool isAliveInPool(const aveng::InstancePoolData<Tag>& pool,
        const aveng::InstanceHandle<Tag>& h)
    {
        if (h.generation == 0) return false;
        if (h.index >= pool.slots.size()) return false;

        const auto& slot = pool.slots[h.index];
        if (!slot.alive) return false;
        if (slot.generation != h.generation) return false;
        if (!slot.instance.has_value()) return false;

        return true;
    }

    template<class Tag>
    bool tryGetInstanceFromPool(const aveng::InstancePoolData<Tag>& pool,
        const aveng::InstanceHandle<Tag>& h,
        aveng::InstanceView& out,
        bool animatedFlag)
    {
        if (!isAliveInPool(pool, h)) return false;

        const auto& inst = pool.slots[h.index].instance.value();

        out.modelId = inst.common.modelId;
        out.xf = inst.common.xf;
        out.animated = animatedFlag; // Implied from args / caller semantics
        return true;
    }

    template<class Tag>
    std::vector<aveng::AnyInstanceHandle> listAllFromPool(const aveng::InstancePoolData<Tag>& pool)
    {
        std::vector<aveng::AnyInstanceHandle> out;
        out.reserve(pool.instancesInOrder.size());

        for (const auto& h : pool.instancesInOrder) {
            // instancesInOrder should only contain live handles, but be defensive:
            if (isAliveInPool(pool, h)) out.emplace_back(h);
        }
        return out;
    }

    template<class Tag>
    std::vector<aveng::AnyInstanceHandle> listForModelFromPool(const aveng::InstancePoolData<Tag>& pool,
        aveng::ModelId id)
    {
        std::vector<aveng::AnyInstanceHandle> out;

        auto it = pool.instancesPerModel.find(id);
        if (it == pool.instancesPerModel.end()) return out;

        const auto& vec = it->second;
        out.reserve(vec.size());
        for (const auto& h : vec) {
            if (isAliveInPool(pool, h)) out.emplace_back(h);
        }
        return out;
    }

}

// Notes:
/*
    The scene facade's getters should not be accessed directly, though you can absolutely do that.
    To keep the architecture clean, any class that needs to read data should have an interface
    injected for a tight read only API:

    For Instances: sceneFacade_.instanceQuery()
    For Models: 
        - To load/unload, use these decorators: getOrLoadModel(), unloadModel()
        - To inspect models from the registry use `modelQuery_`
        - `modelLib` only serves to forward from overridden decorators. In other words, don't inject it
           anywhere via this class because this class overrides it, you Took.

*/
namespace aveng {
    SceneFacade::SceneFacade(IModelLibrary& modelLib, const IModelQuery& modelQuery)
        : modelLib_(modelLib)
        , modelQuery_(modelQuery)
    {
    }

    /* IInstanceQuery */
    bool SceneFacade::tryGetInstance(AnyInstanceHandle h, InstanceView& out) const
    {
        /* O(1) friendly */
        const auto& statPool = staticMgr_.data();
        const auto& animPool = animatedMgr_.data();

        return std::visit([&](auto&& hh) -> bool {
            using H = std::decay_t<decltype(hh)>;

            if constexpr (std::is_same_v<H, std::monostate>) {
                return false;
            }
            else if constexpr (std::is_same_v<H, StaticHandle>) {
                return tryGetInstanceFromPool(statPool, hh, out, /*animatedFlag=*/false);
            }
            else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                return tryGetInstanceFromPool(animPool, hh, out, /*animatedFlag=*/true);
            }
            else {
                if (!cfg_.failSoft) assert(false);
                return false;
            }
        }, h);
    }

    /* IInstanceQuery */
    std::vector<AnyInstanceHandle> SceneFacade::listAllInstances() const
    {
        /*
         * Return an exhaustive list of all instances from 
         * static through animated instances (presently)
         * 
         * Sorted from static -> animated. No available sorting logic otherwise
         */

        const auto& statPool = staticMgr_.data();
        const auto& animPool = animatedMgr_.data();

        auto a = listAllFromPool(statPool);
        auto b = listAllFromPool(animPool);

        a.reserve(a.size() + b.size());
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

    /* IInstanceQuery */
    std::vector<AnyInstanceHandle> SceneFacade::listInstancesForModel(ModelId id) const
    {

        /*
         * Retrieve a list of every instance that exists
         * for a given model id
         */

        const auto& statPool = staticMgr_.data();
        const auto& animPool = animatedMgr_.data();

        auto a = listForModelFromPool(statPool, id);
        auto b = listForModelFromPool(animPool, id);

        a.reserve(a.size() + b.size());
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

    /* IInstanceQuery */
    bool SceneFacade::isAlive(AnyInstanceHandle h) const
    {

        /*
         * alive if: 
         * handle.generation == slot.generation && slot.instance.has_value() && slot.alive == true
         */

        const auto& statPool = staticMgr_.data();
        const auto& animPool = animatedMgr_.data();

        return std::visit([&](auto&& hh) -> bool {
            using H = std::decay_t<decltype(hh)>;

            if constexpr (std::is_same_v<H, std::monostate>) {
                return false;
            }
            else if constexpr (std::is_same_v<H, StaticHandle>) {
                return isAliveInPool(statPool, hh);
            }
            else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                return isAliveInPool(animPool, hh);
            }
            else {
                if (!cfg_.failSoft) assert(false);
                return false;
            }
        }, h);
    }
    /* IInstanceQuery */


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
    /* IModelLibrary */

    bool SceneFacade::unloadModel(const AssetKey& key)
    {
        const bool ok = modelLib_.unloadModel(key);

        // We only have the key here, and SceneFacade has no key->ModelId mapping in this header.
        // So we can't precisely invalidate a single ModelId unless IModelQuery supports lookup by key.
        //
        // Safe conservative choice: clear cached validation (even though we�re not relying on it).
        clearValidationCache();

        return ok;
    }

    const AvengModel* SceneFacade::pModel(ModelId id) const { return modelLib_.pModel(id); }

    void SceneFacade::destroyAllInstancesForModel(ModelId mid)
    {
        staticMgr_.deleteAllInstancesForModel(mid);
        animatedMgr_.deleteAllInstancesForModel(mid);
        
    }

    /* Instance Ops */
    /* Unused at the moment! Since we can easily query via modelQuery_ */
    std::optional<SceneFacade::ModelValidation> SceneFacade::validateModel(ModelId id) const
    {
        // Ignore validated_ map per your request.
        // We treat modelQuery_ as the single source of truth.
        ModelMeta m{};
        std::cout << "Validating Model...\n";
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
        std::cout << "Spawn...\n";
        if (!modelQuery_.isModelLoaded(modelRef.id, m)) {
            if (cfg_.failSoft) return AnyInstanceHandle{};
            assert(false && "SceneFacade::spawn: model id not loaded");
            return AnyInstanceHandle{};
        }

        // Optional: sanity check in debug builds (helps catch stale/corrupt ModelRef).
#ifndef NDEBUG
        if (modelRef.isAnimated != m.animated) {
            // If this ever fires, it means ModelRef receipt is inconsistent with registry.
            assert(false && "SceneFacade::spawn: ModelRef.isAnimated disagrees with ModelMeta.animated");
        }
        // Catch animated models being spawned with static settings
        assert(!m.animated && "SceneFacade::spawn: Use AnimatedCreateSettings overload for animated models");
#endif

        return spawnValidated<StaticTag>(modelRef.id, s, m);
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
        std::cout << "Spawn Animated...\n";
        if (!modelQuery_.isModelLoaded(modelRef.id, m)) {
            if (cfg_.failSoft) return AnyInstanceHandle{};
            assert(false && "SceneFacade::spawn: model id not loaded");
            return AnyInstanceHandle{};
        }

        // Optional: sanity check in debug builds (helps catch stale/corrupt ModelRef).
#ifndef NDEBUG
        if (modelRef.isAnimated != m.animated) {
            // If this ever fires, it means ModelRef receipt is inconsistent with registry.
            assert(false && "SceneFacade::spawn: ModelRef.isAnimated disagrees with ModelMeta.animated");
        }
#endif

        return spawnValidated<AnimatedTag>(modelRef.id, s, m);
    }


    std::vector<AnyInstanceHandle> SceneFacade::spawnMany(
        ModelRef modelRef,
        std::span<const TransformSettings> settings,
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

#ifndef NDEBUG
        if (modelRef.isAnimated != m.animated) {
            assert(false && "SceneFacade::spawnMany: ModelRef.isAnimated disagrees with ModelMeta.animated");
        }
#endif

        for (std::uint32_t i = 0; i < count; ++i) {
            const TransformSettings& s = settings[i % settings.size()]; // Cool
            out.emplace_back(spawnValidated<StaticTag>(modelRef.id, s, m));
        }

        return out;
    }

    std::vector<AnyInstanceHandle> SceneFacade::spawnMany(
        ModelRef modelRef,
        std::span<const AnimatedCreateSettings> settings,
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

#ifndef NDEBUG
        if (modelRef.isAnimated != m.animated) {
            assert(false && "SceneFacade::spawnMany: ModelRef.isAnimated disagrees with ModelMeta.animated");
        }
#endif

        for (std::uint32_t i = 0; i < count; ++i) {
            const AnimatedCreateSettings& s = settings[i % settings.size()];
            out.emplace_back(spawnValidated<AnimatedTag>(modelRef.id, s, m));
        }

        return out;
    }

    /* 
        We always use the plural `cloneInstances` path because we don't want the
        individual path to resize our dirtyGpu arrays if a new slot is created.
    */
    AnyInstanceHandle SceneFacade::clone(AnyInstanceHandle src)
    {

        tmpStaticHandles_.clear();
        tmpAnimatedHandles_.clear();
        tmpStaticHandles_.reserve(1);
        tmpAnimatedHandles_.reserve(1);

        return std::visit([&](auto&& h) -> AnyInstanceHandle {
            using H = std::decay_t<decltype(h)>;

            if constexpr (std::is_same_v<H, std::monostate>) {
                return AnyInstanceHandle{};
            }
            else if constexpr (std::is_same_v<H, StaticHandle>) {
                tmpStaticHandles_.push_back(h);
                const std::vector<StaticHandle> nh = staticMgr_.cloneInstances(tmpStaticHandles_);
                return AnyInstanceHandle{ nh[0] };
            }
            else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                tmpAnimatedHandles_.push_back(h);
                const std::vector <AnimatedHandle> nh = animatedMgr_.cloneInstances(tmpAnimatedHandles_);
                return AnyInstanceHandle{ nh[0] };
            }
            else {
                // If you later add more handle types, you'll land here until you update.
                if (cfg_.failSoft) return AnyInstanceHandle{};
                assert(false && "SceneFacade::clone: unhandled handle variant alternative");
                return AnyInstanceHandle{};
            }
        }, src);
    }

    // If cloning becomes a hot commodity, this needs to be updated
    std::vector<AnyInstanceHandle>
        SceneFacade::cloneMany(std::span<const AnyInstanceHandle> srcHandles)
    {
        tmpStaticHandles_.clear();
        tmpAnimatedHandles_.clear();
        tmpStaticHandles_.reserve(srcHandles.size());
        tmpAnimatedHandles_.reserve(srcHandles.size());

        // Keep output aligned with input ordering:
        // Store �which pool + which index within that pool�s output�.
        struct MapEntry { uint8_t pool; uint32_t idx; }; // pool: 0=none,1=static,2=anim
        std::vector<MapEntry> map;
        map.reserve(srcHandles.size());

        for (const auto& h : srcHandles) {
            MapEntry me{ 0, 0 };
            std::visit([&](auto&& hh) {
                using H = std::decay_t<decltype(hh)>;
                if constexpr (std::is_same_v<H, std::monostate>) {
                    me.pool = 0;
                }
                else if constexpr (std::is_same_v<H, StaticHandle>) {
                    me.pool = 1;
                    me.idx = (uint32_t)tmpStaticHandles_.size();
                    tmpStaticHandles_.push_back(hh);
                }
                else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                    me.pool = 2;
                    me.idx = (uint32_t)tmpAnimatedHandles_.size();
                    tmpAnimatedHandles_.push_back(hh);
                }
            }, h);
            map.push_back(me);
        }

        // Clone per pool once
        std::vector<StaticHandle> statOut;
        std::vector<AnimatedHandle> animOut;

        if (!tmpStaticHandles_.empty()) {
            statOut = staticMgr_.cloneInstances(tmpStaticHandles_);
        }
        if (!tmpAnimatedHandles_.empty()) {
            animOut = animatedMgr_.cloneInstances(tmpAnimatedHandles_);
        }

        // Reassemble in input order
        std::vector<AnyInstanceHandle> out;
        out.reserve(srcHandles.size());

        // Return all the handles *in the same vector*
        for (const auto& me : map) {
            if (me.pool == 1) out.emplace_back(AnyInstanceHandle{ statOut[me.idx] });
            else if (me.pool == 2) out.emplace_back(AnyInstanceHandle{ animOut[me.idx] });
            else out.emplace_back(AnyInstanceHandle{}); // monostate / invalid
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

    void SceneFacade::setTransforms(
        std::span<const AnyInstanceHandle> handles,
        std::span<const TransformSettings> transforms)
    {
        if (handles.size() != transforms.size()) {
            if (cfg_.failSoft) return;
            assert(false && "handles/transforms size mismatch");
            return;
        }

        // Clear scratch (keep capacity)
        tmpStaticHandles_.clear();
        tmpStaticXforms_.clear();
        tmpAnimatedHandles_.clear();
        tmpAnimatedXforms_.clear();

        // Reserve once (best-effort; avoids growth)
        tmpStaticHandles_.reserve(handles.size());
        tmpStaticXforms_.reserve(handles.size());
        tmpAnimatedHandles_.reserve(handles.size());
        tmpAnimatedXforms_.reserve(handles.size());

        for (size_t i = 0; i < handles.size(); ++i) {
            const TransformSettings it = transforms[i];

            std::visit([&](auto&& h) {
                using H = std::decay_t<decltype(h)>;

                if constexpr (std::is_same_v<H, std::monostate>) {
                    // skip
                }
                else if constexpr (std::is_same_v<H, StaticHandle>) {
                    tmpStaticHandles_.push_back(h);
                    tmpStaticXforms_.push_back(toInstanceTransform(it));
                }
                else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                    tmpAnimatedHandles_.push_back(h);
                    tmpAnimatedXforms_.push_back(toInstanceTransform(it));
                }
                else {
                    if (!cfg_.failSoft) assert(false && "Unhandled handle alternative");
                }
            }, handles[i]);
        }

        if (!tmpStaticHandles_.empty()) {
            staticMgr_.setTransforms(tmpStaticHandles_, tmpStaticXforms_);
        }
        if (!tmpAnimatedHandles_.empty()) {
            animatedMgr_.setTransforms(tmpAnimatedHandles_, tmpAnimatedXforms_);
        }
    }
    
    /// Good to know - the frontend can present a congruent type.
    // Overload in case you have hot code and would like to 
    // skip the conversion from TransformSettings -> InstanceTransform
    void SceneFacade::setTransforms(
        std::span<const AnyInstanceHandle> handles,
        std::span<const InstanceTransform> transforms)
    {
        if (handles.size() != transforms.size()) {
            if (cfg_.failSoft) return;
            assert(false && "handles/transforms size mismatch");
            return;
        }

        // Clear scratch (keep capacity)
        tmpStaticHandles_.clear();
        tmpStaticXforms_.clear();
        tmpAnimatedHandles_.clear();
        tmpAnimatedXforms_.clear();

        // Reserve once (best-effort; avoids growth)
        tmpStaticHandles_.reserve(handles.size());
        tmpStaticXforms_.reserve(handles.size());
        tmpAnimatedHandles_.reserve(handles.size());
        tmpAnimatedXforms_.reserve(handles.size());

        for (size_t i = 0; i < handles.size(); ++i) {

            std::visit([&](auto&& h) {
                using H = std::decay_t<decltype(h)>;

                if constexpr (std::is_same_v<H, std::monostate>) {
                    // skip
                }
                else if constexpr (std::is_same_v<H, StaticHandle>) {
                    tmpStaticHandles_.push_back(h);
                    tmpStaticXforms_.push_back(transforms[i]);
                }
                else if constexpr (std::is_same_v<H, AnimatedHandle>) {
                    tmpAnimatedHandles_.push_back(h);
                    tmpAnimatedXforms_.push_back(transforms[i]);
                }
                else {
                    if (!cfg_.failSoft) assert(false && "Unhandled handle alternative");
                }
            }, handles[i]);
        }

        if (!tmpStaticHandles_.empty()) {
            staticMgr_.setTransforms(tmpStaticHandles_, tmpStaticXforms_);
        }
        if (!tmpAnimatedHandles_.empty()) {
            animatedMgr_.setTransforms(tmpAnimatedHandles_, tmpAnimatedXforms_);
        }
    }

    const IModelQuery& SceneFacade::modelQuery() const { return modelQuery_; }

    /*
    * This pattern scales cleanly

        If later you add:
            InstancedParticleTag
            SkinnedClothTag
        You just add:
            another handle type
            another manager
            another set of typed overloads
        No changes to gameplay code that already uses typed handles.
    */

    /// TODO - with typed handles
    //void SceneFacade::setTransform(StaticHandle h, const InstanceTransform& it) {
    //    staticMgr_.setTransform(h, it);
    //}

    //void SceneFacade::setTransform(AnimatedHandle h, const InstanceTransform& it) {
    //    animatedMgr_.setTransform(h, it);
    //}

    //StaticHandle SceneFacade::clone(StaticHandle h) {
    //    return staticMgr_.cloneInstance(h);
    //}

    //AnimatedHandle SceneFacade::clone(AnimatedHandle h) {
    //    return animatedMgr_.cloneInstance(h);
    //}

    //void SceneFacade::deleteInstance(StaticHandle h) {
    //    staticMgr_.deleteInstance(h);
    //}

    //void SceneFacade::deleteInstance(AnimatedHandle h) {
    //    animatedMgr_.deleteInstance(h);
    //}
    /// TODO

}