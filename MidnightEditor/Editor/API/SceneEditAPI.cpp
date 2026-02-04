#include "avpch.h"
#include "SceneEditAPI.h"
#include "Runtime/Facade/SceneFacade.h"

namespace aveng {

	SceneEditAPI::SceneEditAPI(SceneFacade& scene) : scene_{ scene } {}

	const uint32_t SceneEditAPI::nModels() const {
		return scene_.modelQuery().nModels();
	}

	bool SceneEditAPI::uiIsModelLoaded(ModelId id, ModelMeta& meta) {
		if (id == NullModelId) return false;
		scene_.modelQuery().isModelLoaded(id, meta);
	}

	ModelRef SceneEditAPI::uiGetOrLoadModel(const AssetKey& key) {
		return scene_.getOrLoadModel(key);
	}

	bool SceneEditAPI::uiUnloadModel(const AssetKey& key) {
		return scene_.unloadModel(key);
	}

	void SceneEditAPI::uiDestroyAllInstancesForModel(ModelId id) {
		scene_.destroyAllInstancesForModel(id);
	}
///TODO
	//bool  SceneEditAPI::uiPurgeAllInstancesForModel(ModelId id) {
	//	return scene_.destroyAllInstancesForModel(id);
	//}

	// These (and cloning) can probably be void returns
	AnyInstanceHandle SceneEditAPI::uiSpawn(ModelRef modelRef, const TransformSettings& s) {
		return scene_.spawn(modelRef, s);
	}

	AnyInstanceHandle SceneEditAPI::uiSpawn(ModelRef modelRef, const AnimatedCreateSettings& s) {
		return scene_.spawn(modelRef, s);
	}

	std::vector<AnyInstanceHandle> SceneEditAPI::uiSpawnMany(ModelRef modelRef, std::span<const TransformSettings> settings, std::uint32_t count) {
		return scene_.spawnMany(modelRef, settings, count);
	}

	std::vector<AnyInstanceHandle> SceneEditAPI::uiSpawnMany(ModelRef modelRef, std::span<const AnimatedCreateSettings> settings, std::uint32_t count) {
		return scene_.spawnMany(modelRef, settings, count);
	}

	AnyInstanceHandle SceneEditAPI::uiClone(AnyInstanceHandle src) {
		return scene_.clone(src);
	}

	std::vector<AnyInstanceHandle> SceneEditAPI::uiCloneMany(std::span<const AnyInstanceHandle> src) {
		return scene_.cloneMany(src);
	}

	void SceneEditAPI::uiDestroy(AnyInstanceHandle h) {
		scene_.destroy(h);
	}

	void SceneEditAPI::uiDestroyMany(std::span<const AnyInstanceHandle> handles) {
		scene_.destroyMany(handles);
	}

	// Things I need to know:
	// Num Models loaded
	// Num instances loaded, per model & total
	
	const std::unordered_map<AssetKey, ModelMeta> SceneEditAPI::uiMapModels() const {
		return scene_.modelQuery().mapModelMeta();
	}

	/* This might not be useful to the UI */
	const std::vector<ModelRef>  SceneEditAPI::uiListModelRefs() const {
		return scene_.modelQuery().listModels();
	}
	/* This might not be useful to the UI */
	const std::vector<AssetKey> SceneEditAPI::uiListModelKeys() const {
		return scene_.modelQuery().listModelKeys();
	}

	bool SceneEditAPI::uiTryGetInstance(AnyInstanceHandle h, InstanceView& out) const {
		return false;
	}

	const std::vector<UiInstanceRow> SceneEditAPI::uiListInstances() const {

		std::vector<AnyInstanceHandle> handles = scene_.listAllInstances();
		std::vector<UiInstanceRow> rows{};
		rows.reserve(handles.size());

		for (const auto& h : handles) {

			// Populate instance views
			InstanceView v{};
			scene_.tryGetInstance(h, v);
			
			// Create output for UI
			rows.emplace_back(UiInstanceRow{ 
				h, 
				v.modelId, 
				v.animated, 
				v.xf.pos, 
				v.xf.rotEuler, 
				v.xf.scale }
			);
		}

		return rows;
	}

	const std::vector<UiModelRow> SceneEditAPI::uiListModels() const {

		const std::unordered_map<AssetKey, ModelMeta> modelMap = uiMapModels();
		std::vector<UiModelRow> rows{};
		rows.reserve(modelMap.size());

		for (const auto& [key, meta] : modelMap) {
			rows.push_back(UiModelRow{
				meta.id,
				key,
				meta.animated
			});
		}

		return rows;
	}

	const std::vector<AnyInstanceHandle> SceneEditAPI::uiListInstancesForModel(ModelId id) const {
		return scene_.listInstancesForModel(id);
	}

	void SceneEditAPI::uiSetInstanceTransform(AnyInstanceHandle h, const InstanceTransform& t) {
		std::span<const AnyInstanceHandle> hs{ &h, 1 };
		std::span<const InstanceTransform> ts{ &t, 1 };
		scene_.setTransforms(hs, ts);
	}

	bool SceneEditAPI::uiGetInstanceTransform(AnyInstanceHandle h, InstanceTransform& out) const {
		InstanceView view;
		scene_.tryGetInstance(h, view);
		out = view.xf;
		return true;
	}


}