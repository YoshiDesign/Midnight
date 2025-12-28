#pragma once
#include "Core/Modeling/ModelRegistry.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Runtime/World/InstanceManager.h"
#include "Services/ModelServices.h"

namespace aveng {

	/* A Facade should always be `final` as well as its `overrides`
	 * This enables the compiler to inline & devirtualize and skip vtable indirection.
	 * This way facades begin their design in a hot-path friendly shape.
	 * 
	 * TODO: We probably won't delete models after a scene has loaded them.
	 * As an invariant, if we can verify that a model exists at least once
	 * then we should never need to validate it again as long as the scene is active. 
	 * I doubt we'll ever need to load a scene with an absurd number of unique models 
	 * but I'm also willing to bite my tongue on this someday
	 */


	/* */
	class SceneFacade final
		: public IModelLibrary
	{

	public:

		SceneFacade(IModelLibrary& modelLib, const IModelQuery& modelQuery);
		~SceneFacade() {}

		struct Config {
			// If true, spawn/clone/destroy will no-op and return monostate/empty on invalid handles/models.
			// If false, you might assert/throw depending on your engine policy.
			bool failSoft = true;
		};

		/* IModelLibrary */
		ModelId getOrLoadModel(const AssetKey& key);
		bool    unloadModel(const AssetKey& key);

		/* Instance Ops */
		AnyInstanceHandle spawn(ModelRef modelRef, const InstanceSettings& s);
		/// Spawn many instances. `settings.size()` can be 1 (repeat) or N (cycled/repeated).
		std::vector<AnyInstanceHandle> spawnMany(ModelRef modelRef,
			std::span<const InstanceSettings> settings,
			std::uint32_t count);

		AnyInstanceHandle clone(AnyInstanceHandle src);
		std::vector<AnyInstanceHandle> cloneMany(std::span<const AnyInstanceHandle> srcHandles);

		void destroy(AnyInstanceHandle h);
		void destroyMany(std::span<const AnyInstanceHandle> handles);

		/* Transform Ops */
		void setTransform(AnyInstanceHandle handle, std::span<const Transform> transforms);
		void setTransforms(
			std::span<const AnyInstanceHandle> handles,
			std::span<const Transform> transforms);

		/// Call this when you *know* a model became invalid in this scene (e.g., after unload).
		/// If you always go through SceneFacade::unloadModel, it will call this for you.
		void invalidateModel(ModelId id) noexcept { validated_.erase(id); }

		/// Clears all cached validation (safe, cheap).
		void clearValidationCache() noexcept { validated_.clear(); }

	private:

		enum class PoolKind : std::uint8_t { Static, Animated };

		struct ModelValidation {
			PoolKind pool = PoolKind::Static;
		};

		// Validates modelId and returns cached pool kind.
		// If invalid: returns std::nullopt.
		std::optional<ModelValidation> validateModel(ModelId id) const;

		// Dispatch helpers
		AnyInstanceHandle spawnValidated(PoolKind pool, ModelId id, const InstanceSettings& settings);

		void destroyStatic(StaticHandle h);
		void destroyAnimated(AnimatedHandle h);

		// Dependencies (owned elsewhere)
		IModelLibrary& modelLib_;

		/*
			This class is a decorator of IModelQuery calls, 
			hence the inheritance of this member's type
		*/
		const IModelQuery& modelQuery_; 
		Config cfg_{};

		// Instance pools (you can also inject references if you don’t want ownership here)
		InstanceManager<StaticTag>   staticMgr_{ modelQuery_ };
		InstanceManager<AnimatedTag> animatedMgr_{ modelQuery_ };

		// Cache of validated models for this scene:
		// modelId -> poolKind (static vs animated)
		// Mutable because validateModel() can fill cache even in const operations.
		mutable std::unordered_map<ModelId, ModelValidation> validated_;
	};

}