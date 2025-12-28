#pragma once
#include "avpch.h"

namespace aveng {

	using AssetKey = std::string; // Identifies a model - Used to request a model that hasn't been loaded into memory yet
	using ModelId = uint32_t; // Used internally by renderer / instance managers for fast lookups

	struct AnimationMeta {
		float durationTicks = 0.0f;
		float ticksPerSecond = 0.0f;
		// Why are these shared_ptr?
		std::vector<std::shared_ptr<AssimpAnimChannel>> animChannels;
	};

	struct ModelMeta {
		glm::mat4 root;          // model-space -> engine-space correction
		uint32_t  boneCount = 0; // 0 for static models
		bool      animated = false;
	};

	struct Transform {
		glm::vec3 position{ 0 };
		glm::quat rotation{ 1,0,0,0 };
		glm::vec3 scale{ 1 };
	};

	struct IModelLibrary {
		virtual ~IModelLibrary() = default;
		virtual ModelId getOrLoadModel(const AssetKey& key) = 0;
		virtual bool unloadModel(const AssetKey& key) = 0; // "delete" is too strong a word here
	};

	/// Renderer/modelDb should implement this without exposing the whole DB.
	struct IModelQuery {
		virtual ~IModelQuery() = default;

		/// Returns true if modelId is currently loaded/valid.
		virtual bool isModelLoaded(ModelId id, ModelMeta& out) const = 0;

		/// Returns true if the loaded model is animated. Only called when isModelLoaded(id) == true.
		virtual bool isModelAnimated(ModelId id, ModelMeta& out) const = 0;

		virtual bool tryGetModelMeta(ModelId id, ModelMeta& out) const = 0;
	};

	struct IModelAnimQuery {
		virtual ~IModelAnimQuery() = default;

		// returns false if modelId invalid, not animated, or clipIndex out of range
		//virtual bool tryGetClipTicksPerSecond(ModelId modelId, uint32_t clipIndex, float& outTPS) const = 0;

		// optional helpers you’ll likely want soon:
		//virtual bool tryGetClipCount(ModelId modelId, uint32_t& outCount) const = 0;
		//virtual bool tryGetClipDurationTicks(ModelId modelId, uint32_t clipIndex, float& outDurationTicks) const = 0;

		//virtual bool tryGetClipDuration(ModelId id, uint32_t clipIndex, float& outDuration) const = 0;
		//virtual bool tryGetClipChannels(ModelId id, uint32_t clipIndex, std::vector<std::shared_ptr<AssimpAnimChannel>> outChannels) const = 0;
		virtual bool tryGetClipMeta(ModelId id, uint32_t clipIndex, AnimationMeta& out) const = 0;
	};



}