#pragma once
#include "Core/Renderer/FramePacketBuilder.h"
#include "Runtime/World/InstanceManager.h"
#include "Services/InstanceServices.h"	// Instance Queries and InstanceView
#include "Services/IRenderSceneView.h"

namespace aveng {

	/* 
	 * Note that SceneFacade's own their instances through an InstanceManager
	 * for each flavor of instance (which is based on their model characteristics).
	 * We should in theory be able to hot-swap scenes and get entirely different scenes
	 * in front of our eyes.
	 * 
	 * For this reason, this should be a SceneWorld, or simply Scene
	 * 
	 * A Facade should always be `final` as well as its `overrides`
	 * This enables the compiler to inline & devirtualize and skip vtable indirection.
	 * This way facades begin their design in a hot-path friendly shape.
	 * 
	 * TODO: We probably won't delete models after a scene has loaded them.
	 * As an invariant, if we can verify that a model exists at least once
	 * then we should never need to validate it again as long as the scene is active. 
	 * I doubt we'll ever need to load a scene with an absurd number of unique models 
	 * but I'm also willing to bite my tongue on this someday
	 */

	class AvengModel; // We use the SceneFacade as <IRenderSceneView> to punt a `const AvengModel*` directly to the renderer, for now!
					  // You'll find this same fwd decl in InstanceManager.h

	/* */
	class SceneFacade final
		: public IModelLibrary, public IInstanceQuery, public IRenderSceneView
	{

	public:

		SceneFacade(IModelLibrary& modelLib, const IModelQuery& modelQuery);
		~SceneFacade() {}

		/* */
		struct Config {
			// If true, spawn/clone/destroy will no-op and return monostate/empty on invalid handles/models.
			// If false, you might assert/throw depending on your engine policy.
			bool failSoft = true;
		};

		/// TODO
		// ----- Hot-path, typed APIs - TODO define these when you're working on a game -----
		//void setTransform(StaticHandle h, const InstanceTransform& it);
		//void setTransform(AnimatedHandle h, const InstanceTransform& it);

		//StaticHandle   clone(StaticHandle h);
		//AnimatedHandle clone(AnimatedHandle h);

		//// Optional: delete, setAnimClip, etc.
		//void deleteInstance(StaticHandle h);
		//void deleteInstance(AnimatedHandle h);
		/// TODO

		/* IRenderSceneView overrides - and pool views for frame packet builder */
		const IModelQuery& modelQuery() const override;

		/* Just a sane reminder that pools include instance data, not (directly) models */
		FramePacketBuilder::PoolInputs<StaticTag, AvengInstance> staticPoolInputs() override { return staticMgr_.poolInputs(); }
		FramePacketBuilder::PoolInputs<AnimatedTag, AssimpInstance> animatedPoolInputs() override { return animatedMgr_.poolInputs(); }

		/* IInstanceQuery overrides - Just for the editor. Not a performance friendly way to go about things, but simple */
		const IInstanceQuery& instanceQuery() const noexcept { return *this; }
		bool tryGetInstance(AnyInstanceHandle h, InstanceView& out) const override;
		std::vector<AnyInstanceHandle> listAllInstances() const override;
		std::vector<AnyInstanceHandle> listInstancesForModel(ModelId id) const override;
		bool isAlive(AnyInstanceHandle h) const override;

		/* (I)ModelLibrary overrides */
		ModelRef getOrLoadModel(const AssetKey& key) override;	// Load a model or retrieve a ref to it
		bool    unloadModel(const AssetKey& key) override;		// Queue a model for unloading next frame
		const AvengModel* pModel(ModelId id) const override;	// ptr to model

		/* Instance Op's - 1 overload for each kind of data an instance can consume - Static vs Animated becomes implicit */
		AnyInstanceHandle spawn(ModelRef modelRef, const TransformSettings& s);
		AnyInstanceHandle spawn(ModelRef modelRef, const AnimatedCreateSettings& s);

		/// Spawn many instances. `settings.size()` can be 1 (repeat) or N (cycled/repeated).
		std::vector<AnyInstanceHandle> spawnMany(ModelRef modelRef,
			std::span<const TransformSettings> settings,
			std::uint32_t count);
		std::vector<AnyInstanceHandle> spawnMany(ModelRef modelRef,
			std::span<const AnimatedCreateSettings> settings,
			std::uint32_t count);

		/* */
		AnyInstanceHandle clone(AnyInstanceHandle src);
		std::vector<AnyInstanceHandle> cloneMany(std::span<const AnyInstanceHandle> srcHandles);

		/* dIE */
		void destroy(AnyInstanceHandle h);
		void destroyMany(std::span<const AnyInstanceHandle> handles);
		void destroyAllInstancesForModel(ModelId id); /* This is also a callback registered on the ModelLibrary */

		/// TODO bool purgeAllInstancesForModel(ModelId id);

		/* Transform Ops */
		/* TransformSettings - legacy, more of a request struct */
		void setTransforms(
			std::span<const AnyInstanceHandle> handles,
			std::span<const TransformSettings> transforms);
		
		/* InstanceTransform - Direct copy of instance's transform */
		void setTransforms(
			std::span<const AnyInstanceHandle> handles,
			std::span<const InstanceTransform> transforms);

		/* Material */
		void setMaterials(std::span<const AnyInstanceHandle> handles, std::span<MnMaterial> mats, std::span<MnMaterialExt> matsExt);
		void setMaterials(std::span<const AnyInstanceHandle> handles, std::span<MnMaterial> mats);

		/// Call this when you *know* a model became invalid in this scene (e.g., after unload).
		/// If you always go through SceneFacade::unloadModel, it will call this for you.
		void invalidateModel(ModelId id) noexcept { validated_.erase(id); }

		/// Clears all cached validation (safe, cheap).
		void clearValidationCache() noexcept { validated_.clear(); }

		inline InstanceTransform toInstanceTransform(const TransformSettings& s) {
			return InstanceTransform{
				.pos = s.worldPosition,
				.rotEuler = s.worldRotation,
				.scale = s.scale
			};
		}

	private:

		struct ModelValidation {
			PoolKind pool = PoolKind::Static;
		};

		/* spawn() remains an overload, but this one shouldn't care */
		template <class Tag>
		AnyInstanceHandle spawnValidated( ModelId id, const CreateSettingsFor<Tag>& s, const ModelMeta& m)
		{
			
			if constexpr (InstanceTypeFor<Tag>::kAnimated) {
				const AnimatedHandle h = animatedMgr_.addInstanceOfModel(id, s, m);
				return AnyInstanceHandle{ h };
			}
			else {
				const StaticHandle h = staticMgr_.addInstanceOfModel(id, s, m);
				return AnyInstanceHandle{ h };
			}

			if (cfg_.failSoft) return AnyInstanceHandle{};
			assert(false && "SceneFacade::spawnValidated: unknown pool kind");
			return AnyInstanceHandle{};
		}

		// Validates modelId and returns cached pool kind.
		// If invalid: returns std::nullopt.
		/* Unused at the moment! */
		std::optional<ModelValidation> validateModel(ModelId id) const;

		// Dispatch helpers
		// AnyInstanceHandle spawnValidated(PoolKind pool, ModelId id, const TransformSettings& s, const ModelMeta& m);

		void destroyStatic(StaticHandle h);
		void destroyAnimated(AnimatedHandle h);

		IModelLibrary& modelLib_;
		const IModelQuery& modelQuery_;
		Config cfg_{};

		/* Instance Stat Updates */
		std::vector<StaticHandle>   tmpStaticHandles_;
		std::vector<AnimatedHandle> tmpAnimatedHandles_;

		std::vector<InstanceTransform> tmpStaticXforms_;
		std::vector<InstanceTransform> tmpAnimatedXforms_;

		std::vector<MnMaterial> tmpStaticMats_;
		std::vector<MnMaterial> tmpAnimMats_;
		std::vector<MnMaterialExt> tmpStaticMatsExt_;
		std::vector<MnMaterialExt> tmpAnimMatsExt_;
		/* */

		// Instance pools owned by the SceneFacade
		InstanceManager<StaticTag>   staticMgr_{ modelQuery_ };
		InstanceManager<AnimatedTag> animatedMgr_{ modelQuery_ };

		// Cache of validated models for this scene:
		// modelId -> poolKind (static vs animated)
		// Mutable because validateModel() can fill cache even in const operations.
		mutable std::unordered_map<ModelId, ModelValidation> validated_; // Unused at the moment. It's an optimization path, but not a very high-priority one
	};
	 
}