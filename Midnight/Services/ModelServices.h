#pragma once
#include "avpch.h"

namespace aveng {

	using AssetKey = std::string; // Identifies a model - Used to request a model that hasn't been loaded into memory yet
	using ModelId = uint32_t; // Used internally by renderer / instance managers for fast lookups

	struct AnimationMeta {
		float durationTicks = 0.0f;
		float ticksPerSecond = 0.0f;
		uint32_t boneCount = 0;
		// Why are these shared_ptr?
		std::span<AssimpAnimChannel*> animChannels;
	};

	struct ModelMeta {
		glm::mat4 root;          // model-space -> engine-space correction created at model load
		uint32_t  boneCount = 0; // 0 for static models
		bool      animated = false;
	};

	struct IModelLibrary {
		virtual ~IModelLibrary() = default;
		virtual ModelRef getOrLoadModel(const AssetKey& key) = 0;
		virtual bool unloadModel(const AssetKey& key) = 0; // "delete" is too strong a word here
	};

	/* 
	* Please prefer/incorporate the use 
	* of output parameters as primary output.
	*/

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

		// Should derive duration, ticks, channels, channel size, etc...
		virtual bool tryGetClipMeta(ModelId id, uint32_t clipIndex, AnimationMeta& out) const = 0;
	};

}