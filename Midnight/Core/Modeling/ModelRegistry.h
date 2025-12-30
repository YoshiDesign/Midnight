#pragma once
#include "avpch.h"
#include "Services/ModelServices.h"

/*For now, simplest rule: models stay loaded until shutdown.*/

namespace aveng {

	// This is private to the model registry mechanics.
	struct ModelEntry {
		std::unique_ptr<AvengModel> model;
		ModelId id;                 // runtime identity
		AssetKey key;               // external identity
		bool isAnimated = false;		// Model Constant
		uint32_t boneCount;				// Model Constant
		glm::mat4 rootTransform{1.f};	// Model Constant
	};

	// This is public for callers who create models - 
	// AvengModel* creates invariance: if there's a ModelRef, there had better be a corresponding ModelEntry or it must be nullptr
	// Also, ModelId 0 is always the Null Model.
	struct ModelRef {
		ModelId id = 0;
		bool isAnimated = false;

		explicit operator bool() const { return id != 0; } // Might need updated bc the NullModel could be nullptr in the future
	};

	/* The ModelDB */
	struct ModelRegistryData final : public IModelAnimQuery, public IModelQuery {
		std::vector<ModelEntry> models;
		std::unordered_map<AssetKey, ModelId> idByKey;
		std::unordered_map<ModelId, size_t> indexById; // To look up the ModelEntry's index from models

		std::vector<AssetKey> pendingLoads;
		std::vector<ModelId> pendingUnload;

		std::optional<ModelId> selectedModel;

		const ModelEntry* get(ModelId id) const { 
			std::cout << "ModelEntry::get(): " << id << std::endl;
			auto it = indexById.find(id);
			if (it == indexById.end()) return nullptr;
			return &models[it->second];
		};

		// IModelAnimQuery
		bool tryGetClipMeta(ModelId id, uint32_t clipIndex, AnimationMeta& out) const {
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
		bool isModelAnimated(ModelId id, ModelMeta& out) const {
			const ModelEntry* e = get(id);
			if (!e) return false;

			/* Todo - cater to the ModelMeta */

			return e->isAnimated;
		}

		// IModelQuery
		bool isModelLoaded(ModelId id, ModelMeta& out) const { 
			const ModelEntry* e = get(id);
			if (!e) return false;
			out.animated = e->isAnimated;
			out.boneCount = e->boneCount;
			out.root = e->rootTransform;
			return true;
		}

		// IModelQuery
		bool tryGetModelMeta(ModelId id, ModelMeta& out) const {
			std::cout << "Renderer::tryGetModelMeta - Querying\n";
			const ModelEntry* e = get(id);
			if (!e) return false;

			out.root = e->rootTransform;
			out.boneCount = e->boneCount;
			out.animated = e->isAnimated;
			return true;
		}

		/* Individual getters, from before ModelMeta was a thing */
		/* IModelAnimQuery */
		//bool tryGetClipTicksPerSecond(ModelId id, uint32_t clipIndex, float& outTPS) const {
		//	std::cout << "ModelRegistryData::tryGetClipTicksPerSecond - Querying\n";
		//	const ModelEntry* e = get(id);
		//	if (!e || !e->isAnimated) return false;

		//	const auto& clips = e->model->getAnimClips();
		//	if (clipIndex >= clips.size()) return false;

		//	outTPS = clips[clipIndex]->getClipTicksPerSecond();
		//	return true;
		//}

		//bool tryGetClipDuration(ModelId id, uint32_t clipIndex, float& outDuration) const {
		//	std::cout << "ModelRegistryData::tryGetClipTicksPerSecond - Querying\n";
		//	const ModelEntry* e = get(id);
		//	if (!e || !e->isAnimated) return false;

		//	const auto& clips = e->model->getAnimClips();
		//	if (clipIndex >= clips.size()) return false;

		//	outDuration = clips[clipIndex]->getClipDuration();
		//	return true;
		//}

		//bool tryGetClipChannels(ModelId id, uint32_t clipIndex, std::vector<std::shared_ptr<AssimpAnimChannel>> outChannels) const {
		//	std::cout << "ModelRegistryData::tryGetClipTicksPerSecond - Querying\n";
		//	const ModelEntry* e = get(id);
		//	if (!e || !e->isAnimated) return false;

		//	const auto& clips = e->model->getAnimClips();
		//	if (clipIndex >= clips.size()) return false;

		//	outChannels = clips[clipIndex]->getChannels();
		//	return true;
		//}


		//bool tryGetClipCount(ModelId modelId, uint32_t& outCount) const {
		//	return true;
		//}

		//bool tryGetClipDurationTicks(ModelId modelId, uint32_t clipIndex, float& outDurationTicks) const {
		//	return true;
		//}

	};

	constexpr ModelId NullModelId = 0;

}