#pragma once
#include <cstdint>
#include <string>
#include <memory>
#include "avpch.h"
#include "Core/Modeling/AssimpAnimChannel.h"

namespace aveng {

	/*
	* TODO: 
	* - Create a custom allocator for models, instead of unique_ptr/heap
	*/

	class AvengModel; // Note: this is causing a benign include cycle in aveng_model.h. Ownership must live elsewhere

	using AssetKey = std::string; // Identifies a model - Used to request a model that hasn't been loaded into memory yet
	using ModelId = uint32_t; // Used internally by renderer / instance managers for fast lookups

	struct ModelBoneMeta {
		uint32_t boneOffsetBase;   // index into global mat4 buffer
		uint32_t boneParentBase;   // index into global parent index buffer
		uint32_t boneCount;
		uint32_t pad;
	};

	// This is private to the model registry mechanics.
	struct ModelEntry {
		std::unique_ptr<AvengModel> model; // We could do away with this entirely by replacing it with each model's "guts", and abandoning an explicit unique_ptr to it.
		// e.g. : 
		// Vertex data
		// Indices
		// etc...
		ModelId id;                 // runtime identity
		AssetKey key;               // external identity
		bool isAnimated = false;		// Model Constant
		glm::mat4 rootTransform{1.f};	// Model Constant
		ModelBoneMeta boneMeta;
		uint32_t skinMetaIndex{ 0 };	// This models index in the `ModelSkinMeta` vector (UBO Data)

		ModelEntry() = default;

		/* Explicit Move Semantics - so we can reorganize index/key storage */
		ModelEntry(const ModelEntry&) = delete;
		ModelEntry& operator=(const ModelEntry&) = delete;

		ModelEntry(ModelEntry&&) noexcept = default;
		ModelEntry& operator=(ModelEntry&&) noexcept = default;

		~ModelEntry() = default;
	};

	// This is public for callers who create models - 
	// AvengModel* creates invariance: if there's a ModelRef, there had better be a corresponding ModelEntry or it must be nullptr
	// Also, ModelId 0 is always the Null Model.
	struct ModelRef {
		ModelId id = 0;
		bool isAnimated = false;

		explicit operator bool() const { return id != 0; } // Might need updated bc the NullModel could be nullptr in the future
	};

	struct AnimationMeta {
		float durationTicks = 0.0f;
		float ticksPerSecond = 0.0f;
		uint32_t boneCount = 0;
		// Why are these shared_ptr?
		std::vector<AssimpAnimChannel> animChannels;
	};

	struct ModelMeta {
		ModelId id;
		glm::mat4 root;          // model-space -> engine-space correction created at model load
		uint32_t  boneCount = 0; // 0 for static models
		uint32_t skinMetaIndex = 0; // Not for static models
		bool      animated = false;
	};

	inline ModelRef makeModelRef(const ModelEntry& e)
	{
		return ModelRef{
			e.id,
			e.isAnimated
		};
	}

	struct IModelLibrary {
		virtual ~IModelLibrary() = default;
		virtual ModelRef getOrLoadModel(const AssetKey& key) = 0;
		virtual bool unloadModel(const AssetKey& key) = 0; // "delete" is too strong a word here
		virtual const AvengModel* pModel(ModelId id) const = 0;
	};

	/// Renderer/modelDb should implement this without exposing the whole DB.
	struct IModelQuery {
		virtual ~IModelQuery() = default;

		virtual const uint32_t nModels() const = 0;

		/// Returns true if modelId is currently loaded/valid.
		virtual bool isModelLoaded(ModelId id, ModelMeta& out) const = 0;

		/// Returns true if the loaded model is animated. Only called when isModelLoaded(id) == true.
		virtual bool isModelAnimated(ModelId id, ModelMeta& out) const = 0;

		virtual bool tryGetModelMeta(ModelId id, ModelMeta& out) const = 0;

		virtual const std::unordered_map<AssetKey, ModelMeta> mapModelMeta() const = 0;

		virtual const std::vector<ModelRef> listModels() const = 0;
		
		virtual const std::vector<AssetKey> listModelKeys() const = 0;
	};

	struct IModelAnimQuery {
		virtual ~IModelAnimQuery() = default;

		// Should derive duration, ticks, channels, channel size, etc...
		virtual bool tryGetClipMeta(ModelId id, uint32_t clipIndex, AnimationMeta& out) const = 0;
	};

	/* The ModelDB - Note: Don't ever template a class that inherits virtual interfaces */
	struct ModelRegistryData final : public IModelAnimQuery, public IModelQuery {
		std::vector<ModelEntry> models;
		std::unordered_map<AssetKey, ModelId> idByKey;
		std::unordered_map<ModelId, size_t> indexById; // To look up the ModelEntry's index from models

		std::vector<AssetKey> pendingLoads;
		std::vector<ModelId> pendingUnload;

		std::optional<ModelId> selectedModel;

		const ModelEntry* get(ModelId id) const;

		const uint32_t nModels() const;

		bool tryGetClipMeta(ModelId id, uint32_t clipIndex, AnimationMeta& out) const override;
		bool isModelAnimated(ModelId id, ModelMeta& out) const override;
		bool isModelLoaded(ModelId id, ModelMeta& out) const override;
		bool tryGetModelMeta(ModelId id, ModelMeta& out) const override;
		virtual const std::unordered_map<AssetKey, ModelMeta> mapModelMeta() const override;
		virtual const std::vector<ModelRef> listModels() const override;
		virtual const std::vector<AssetKey> listModelKeys() const override;

	};

	constexpr ModelId NullModelId = 0;

}