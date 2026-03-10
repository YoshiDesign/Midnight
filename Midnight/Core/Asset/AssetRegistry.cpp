#include "AssetRegistry.h"
#include "Core/aveng_model.h"

/* 
	This file provides us with the necessary evasive maneuvering around circular dependencies.
	aveng_model.h requires types in AssetRegistry.h but our ModelEntry and ModelRegistryData
	require the AvengModel definitions. They can have them via this TU so we can fwd declare in AssetRegistry.h
*/
namespace aveng {

	const ModelEntry* ModelRegistryData::get(ModelId id) const {
		auto it = indexById.find(id);
		if (it == indexById.end()) return nullptr;
		return &models[it->second];
	};

	// IModelAnimQuery
	bool ModelRegistryData::tryGetClipMeta(ModelId id, uint32_t clipIndex, AnimationMeta& out) const {

		/* 
		 *	TODO - This an others: Once we remove the necessity for a unique ptr to a model, in favor
		 *	of storing its data directly in each ModelEntry, we'll need to update our queries.
		 */

		const ModelEntry* e = get(id);
		if (!e || e->id == NullModelId || e->model == nullptr || !e->isAnimated) return false;

		const auto& clips = e->model->getAnimClips();

		if (clipIndex >= clips.size()) return false;
		out.durationTicks = clips[clipIndex]->getClipDuration();
		out.ticksPerSecond = clips[clipIndex]->getClipTicksPerSecond();
		out.animChannels = clips[clipIndex]->getChannels();
		out.boneCount = e->boneCount;
		return true;
	}

	// IModelQuery
	const uint32_t ModelRegistryData::nModels() const {
		return models.size();
	}

	bool ModelRegistryData::isModelAnimated(ModelId id, ModelMeta& out) const {
		const ModelEntry* e = get(id); // Micro-Optimization - quit early if nullModelId
		if (!e || e->id == NullModelId || e->model == nullptr) return false;
		out.id = e->id;
		out.animated = e->isAnimated;
		out.boneCount = e->boneCount;
		out.root = e->rootTransform;
		return e->isAnimated;
	}

	// IModelQuery
	bool ModelRegistryData::isModelLoaded(ModelId id, ModelMeta& out) const {
		const ModelEntry* e = get(id);
		if (!e || e->id == NullModelId || e->model == nullptr) return false;
		out.id = e->id;
		out.animated = e->isAnimated;
		out.boneCount = e->boneCount;
		out.root = e->rootTransform;
		return true;
	}

	// IModelQuery
	bool ModelRegistryData::tryGetModelMeta(ModelId id, ModelMeta& out) const {
		const ModelEntry* e = get(id);
		if (!e || e->id == NullModelId || e->model == nullptr) return false;
		out.id = e->id;
		out.root = e->rootTransform;
		out.boneCount = e->boneCount;
		out.animated = e->isAnimated;
		return true;
	}

	const std::unordered_map<AssetKey, ModelMeta> ModelRegistryData::mapModelMeta() const {
		
		std::unordered_map<AssetKey, ModelMeta> modelMeta{};
		modelMeta.reserve(idByKey.size());

		ModelMeta meta{};
		for (const auto& [key, id] : idByKey) {
			tryGetModelMeta(id, meta);
			modelMeta.try_emplace(key, meta);
		}
		
		return modelMeta;
	}

	const std::vector<ModelRef> ModelRegistryData::listModels() const {
		std::vector<ModelRef> refs;
		for (const auto& entry : models) {
			refs.emplace_back(makeModelRef(entry));
		}
		return refs;
	}

	const std::vector<AssetKey> ModelRegistryData::listModelKeys() const {
		std::vector<AssetKey> keys;
		for (const auto& entry : models) {
			keys.push_back(entry.key);
		}
		return keys;
	}

}