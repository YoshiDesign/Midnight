#include "ModelRegistry.h"
#include "Core/aveng_model.h"

/* 
	This file provides us with the necessary evasive maneuvering around circular dependencies.
	aveng_model.h requires types in ModelRegistry.h but our ModelEntry and ModelRegistryData
	require the AvengModel definitions. They can have them via this TU so we can fwd declare in ModelRegistry.h
*/
namespace aveng {


	ModelEntry::~ModelEntry() = default;

	const ModelEntry* ModelRegistryData::get(ModelId id) const {
		std::cout << "ModelEntry::get(): " << id << std::endl;
		auto it = indexById.find(id);
		if (it == indexById.end()) return nullptr;
		return &models[it->second];
	};

	// IModelAnimQuery
	bool ModelRegistryData::tryGetClipMeta(ModelId id, uint32_t clipIndex, AnimationMeta& out) const {
		std::cout << "ModelRegistryData::tryGetClipTicksPerSecond - Querying\n";
		const ModelEntry* e = get(id);
		if (!e || !e->isAnimated) return false;

		const auto& clips = e->model->getAnimClips();
		if (clipIndex >= clips.size()) return false;

		out.durationTicks = clips[clipIndex]->getClipDuration();
		out.ticksPerSecond = clips[clipIndex]->getClipTicksPerSecond();
		out.animChannels = clips[clipIndex]->getChannels();
		out.boneCount = e->boneCount;

		return true;
	}

	// IModelQuery
	bool ModelRegistryData::isModelAnimated(ModelId id, ModelMeta& out) const {
		const ModelEntry* e = get(id);
		if (!e) return false;

		/* Todo - cater to the ModelMeta */

		return e->isAnimated;
	}

	// IModelQuery
	bool ModelRegistryData::isModelLoaded(ModelId id, ModelMeta& out) const {
		const ModelEntry* e = get(id);
		if (!e) return false;
		out.animated = e->isAnimated;
		out.boneCount = e->boneCount;
		out.root = e->rootTransform;
		return true;
	}

	// IModelQuery
	bool ModelRegistryData::tryGetModelMeta(ModelId id, ModelMeta& out) const {
		std::cout << "Renderer::tryGetModelMeta - Querying\n";
		const ModelEntry* e = get(id);
		if (!e) return false;

		out.root = e->rootTransform;
		out.boneCount = e->boneCount;
		out.animated = e->isAnimated;
		return true;
	}

}