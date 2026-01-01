#include "SceneEditAPI.h"
#include "Runtime/Facade/SceneFacade.h"

namespace aveng {

	SceneEditAPI::SceneEditAPI(SceneFacade& scene) : scene_{ scene } {}

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
	
	const std::unordered_map<AssetKey, ModelId> SceneEditAPI::uiMapModels() const {
		return scene_.modelQuery().mapModels();
	}

	const std::vector<ModelRef>  SceneEditAPI::uiListModelRefs() const {
		return scene_.modelQuery().listModels();
	}

	const std::vector<AssetKey> SceneEditAPI::uiListModelKeys() const {
		
		return scene_.modelQuery().listModelKeys();
	}

	const std::vector<AnyInstanceHandle> SceneEditAPI::uiListInstances() const {
		return scene_.listAllInstances();
	}

	const std::vector<AnyInstanceHandle> SceneEditAPI::uiListInstancesForModel(ModelId id) const {
		return scene_.listInstancesForModel(id);
	}

}