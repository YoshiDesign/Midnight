#pragma once
#include "avpch.h"


/*For now, simplest rule: models stay loaded until shutdown.*/

namespace aveng {

	using AssetKey = std::string; // Identifies a model - Used to request a model that hasn't been loaded into memory yet
	using ModelId = uint32_t; // Used internally by renderer / instance managers for fast lookups

	// This is private to the model registry mechanics.
	struct ModelEntry {
		ModelId id;                 // runtime identity
		AssetKey key;               // external identity
		std::shared_ptr<AvengModel> model;
		bool isAnimated = false;    // determined at load
	};

	// This is public for callers who create models
	struct ModelRef {
		ModelId id = 0;
		AvengModel* model = nullptr;   // or std::shared_ptr<AvengModel>
		bool isAnimated = false;

		explicit operator bool() const { return id != 0 && model != nullptr; }
	};

	//struct ModelDb {
	//	std::vector<ModelEntry> entries;                 // stable storage
	//	std::unordered_map<AssetKey, ModelId> idByKey;   // AssetKey -> ModelId
	//	std::unordered_map<ModelId, size_t> indexById;   // ModelId  -> entries index
	//};


	constexpr ModelId InvalidModelId = 0;

}