#include "ModelLibrary.h"
#include "CoreVK/EngineDevice.h"

namespace aveng {

	static AssetKey normalizeAssetKey(std::string_view in) {
		AssetKey out(in);
		for (char& c : out) {
			if (c == '\\') c = '/';
		}
		// Optional: lowercase, strip "./", etc.
		std::cout << "Renderer Normalized Asset Key: " << out << std::endl;
		return out;
	}

	std::unique_ptr<IModelSource> ModelLibrary::createModelSource() {
#ifdef ENABLE_EDITOR
		return std::make_unique<FilesystemModelSource>();
#else
		return std::make_unique<PackModelSource>("assets.pak");
#endif
	}

	ModelLibrary::ModelLibrary(
		EngineDevice& engineDevice,
		VkRenderData& renderData
	) : engineDevice_{ engineDevice }, renderData_{renderData} {
		
		// Register a null model for "no selected models"
		ModelEntry entry;
		entry.id = NullModelId;
		entry.key = "null/";
		entry.model.reset();
		entry.isAnimated = false;
		entry.boneCount = 0;

		registry_.models.emplace_back(std::move(entry));
		registry_.idByKey.emplace("null", NullModelId);
		registry_.indexById.emplace(NullModelId, 0); // Lives at index 0

		// Determine model source
		modelSource_ = createModelSource();
	}

	/* Add model to pending deletes */
	bool ModelLibrary::unloadModel(const AssetKey& assetKey) 
	{

		const AssetKey key = normalizeAssetKey(assetKey);

		auto it = registry_.idByKey.find(key);
		if (it == registry_.idByKey.end()) 
		{
			return false; // never loaded / unknown
		}

		const ModelId id = it->second;

		auto idxIt = registry_.indexById.find(id);
		if (idxIt == registry_.indexById.end()) 
		{
			return false; // map inconsistency (anomaly)
		}

		ModelEntry& entry = registry_.models[idxIt->second];
		if (!entry.model) 
		{
			return false; // already unloaded or failed load
		}

		// Prevent duplicate queueing
		if (std::find(registry_.pendingUnload.begin(), registry_.pendingUnload.end(), id) != registry_.pendingUnload.end())
		{
			return true;
		}
		
		registry_.pendingUnload.emplace_back(id);
		return true;
	}

	ModelRef ModelLibrary::getOrLoadModel(const AssetKey& assetKey)
	{
		const AssetKey key = normalizeAssetKey(assetKey);
		std::cout << "[Renderer::getOrLoadModel] Loading Model: " << assetKey << std::endl;

		// Already known?
		if (auto it = registry_.idByKey.find(key); it != registry_.idByKey.end()) { // very "Go" semantic lol
			const ModelId id = it->second;
			const auto idxIt = registry_.indexById.find(id);
			if (idxIt == registry_.indexById.end()) {
				// Should never happen if maps are consistent - We had a key to no ID
				std::cout << "[Renderer::getOrLoadModel] Anomaly alert [1]\n";
				return {};
			}

			ModelEntry& entry = registry_.models[idxIt->second];

			// If already loaded, return valid ref
			if (entry.model) {
				// Proof of purchase
				return makeModelRef(entry);
			}

			// We didn't find a model despite having found its index into the model list. Maybe it's in queue. If not, queue it
			if (std::find(registry_.pendingLoads.begin(), registry_.pendingLoads.end(), key) == registry_.pendingLoads.end()) {
				std::cout << "[Renderer::getOrLoadModel] Key found without a model - Anomaly alert [2]\n";
				registry_.pendingLoads.push_back(key);
			}

			// Return a stable id even though it's not loaded yet
			return ModelRef{ entry.id, entry.isAnimated };
		}

		// New registration: allocate a stable id
		const ModelId id = nextModelId_++;
		const size_t index = registry_.models.size();

		ModelEntry entry;
		entry.id = id;
		entry.key = key;
		entry.model.reset();
		entry.isAnimated = false;

		registry_.models.emplace_back(std::move(entry));
		registry_.idByKey.emplace(key, id);
		registry_.indexById.emplace(id, index);

		// Queue for later load
		registry_.pendingLoads.push_back(key);

		// Not loaded yet -> invalid ref, but caller can keep the id
		return aveng::ModelRef{ id, false };
	}

	void ModelLibrary::processPendingUnloads()
	{
		if (registry_.pendingUnload.empty())
			return;

		// Consume queue atomically (O(1))
		std::vector<ModelId> queue;
		queue.swap(registry_.pendingUnload);

		for (ModelId id : queue)
		{
			std::cout << "removing model " << id << " from processUnload queue\n";
			// Still exists?
			auto idxIt = registry_.indexById.find(id);
			if (idxIt == registry_.indexById.end()) {
				continue; // already removed or bad id
			}

			const size_t idx = idxIt->second;
			ModelEntry& entry = registry_.models[idx];

			// Sanity: entry.id should match id if you store it
			// assert(entry.id == id);

			// 1) Destroy all instances for this model BEFORE cleanup
			//    (How you do this depends on your architecture; see below.)
#ifdef M_DEBUG
			assert(onDestroyInstancesForModel_ && "ModelLibrary: destroy-instances callback not set!");
#endif
			// Callback to destroy all instances of this model. See: Midnight::registerCallbacks
			onDestroyInstancesForModel_(id);
			
			// 2) GPU safety:
			//    Only do this if you are sure no in-flight cmd buffers reference this model.
			//    (Call this method only after the relevant frame fence(s) have completed.)
			if (entry.model) {
				entry.model->cleanup(engineDevice_, renderData_);
				entry.model.reset(); // drop the shared_ptr after cleanup
			}

			// 3) Remove key->id mapping
			//    Prefer entry.key if it exists; otherwise you must search idByKey (slower).
			registry_.idByKey.erase(entry.key);

			// 4) Remove id->index mapping for this id
			registry_.indexById.erase(id);

			// 5) Remove ModelEntry from models with swap-erase
			const size_t last = registry_.models.size() - 1;
			if (idx != last) {
				// Move last into idx
				registry_.models[idx] = std::move(registry_.models[last]);

				// Fix indexById for the moved entry
				const ModelId movedId = registry_.models[idx].id; // assuming ModelEntry has id
				registry_.indexById[movedId] = idx;
			}
			registry_.models.pop_back();

			// 6) Fix selected model
			if (registry_.selectedModel && *registry_.selectedModel == id) {
				registry_.selectedModel.reset();
			}
		}
	}

	void ModelLibrary::processPendingModelLoads()
	{
		if (registry_.pendingLoads.empty()) {
			return;
		}

		// Consume queue atomically (so getOrLoadModel can keep pushing next frame)
		std::vector<AssetKey> queue;
		queue.swap(registry_.pendingLoads); // O(1) cool

		bool anyLoaded = false;
		ModelId lastLoadedId = NullModelId;

		for (const AssetKey& key : queue) {
			auto it = registry_.idByKey.find(key);
			if (it == registry_.idByKey.end()) {
				// Key got removed or never registered (should be rare) - implies model was loaded through another means than this class
				std::printf("[ModelLibrary] Pending load key missing from registry: %s\n", key.c_str());
				continue;
			}

			const ModelId id = it->second;
			auto idxIt = registry_.indexById.find(id);
			if (idxIt == registry_.indexById.end()) {
				std::printf("[ModelLibrary] Pending load id missing from index map: %u\n", id);
				continue;
			}

			ModelEntry& entry = registry_.models[idxIt->second];

			// Might have loaded already (if duplicate queue)
			if (entry.model) {
				continue;
			}

			std::printf("[ModelLibrary] Processing queued model load: %s\n", key.c_str());

			// 1) Read bytes (or ignore bytes if your builder loads from path)
			std::vector<std::byte> bytes = modelSource_->readModelBytes(key);

			// 2) Build/import
			std::unique_ptr<AvengModel> model = buildModelFromSource(key, bytes);
			if (!model) {
				std::printf("[ModelLibrary] Failed to load model: %s\n", key.c_str());
				// Policy: do NOT requeue automatically (avoid infinite spam).
				// If you want retry-once, you can push_back(key) here conditionally.
				continue;
			}

			// Commit to its registry entry
			entry.model = std::move(model);
			entry.isAnimated = entry.model->hasAnimations();

			anyLoaded = true;
			lastLoadedId = entry.id;

			std::printf("[ModelLibrary] Loaded model id=%u key=%s animated=%s\n",
				entry.id, entry.key.c_str(), entry.isAnimated ? "true" : "false"
			);

			// Optional: if you have GPU upload steps, do them HERE (still between frames)
			// uploadModelToGpu(entry);
		}

		// Arbitrary
		if (anyLoaded && lastLoadedId != NullModelId) {
			// After uploading latest model, make it the editor's currently selected model
			registry_.selectedModel = lastLoadedId;
		}
	}

	// If you want: compute baseDir from the key (filesystem path today)
	std::string ModelLibrary::baseDirForAssetKey(const AssetKey& key) const
	{
		// You can swap this policy later without touching the import path.
		// For now: baseDir derived from key, which is expected to be a path-like string.
		auto pos = key.find_last_of("/\\");
		if (pos == std::string::npos) {
			// Fallback: you can hard-code here if you want a global assets directory
			// return "Assets";
			return {};
		}
		return key.substr(0, pos);
	}

	std::unique_ptr<AvengModel> ModelLibrary::buildModelFromSource(
		const AssetKey& key,
		std::span<const std::byte> bytes
	) {
		// Construct model
		auto model = std::make_unique<AvengModel>(engineDevice_);

		// Base directory policy for resolving external refs (textures/bin/etc)
		const std::string baseDir = baseDirForAssetKey(key);

		// Import flags�keep your existing defaults, pass extra if needed
		constexpr unsigned int extraImportFlags = 0;

		const std::string modelBaseDir = baseDirForAssetKey(key);                // e.g. "Assets/Models/House"
		const std::string engineTextureDir = joinPath(contentRoot_, textureRoot_); // e.g. "Assets/textures"

		// NOTE: AvengModel now does "import/build", not IO.
		const bool ok = model->loadModelV2(
			renderData_,
			key,
			bytes,
			extraImportFlags,
			modelBaseDir,
			engineTextureDir
		);

		if (!ok) {
			std::printf("[Renderer] buildModelFromSource failed for key=%s\n", key.c_str());
			return {};
		}

		return model;
	}

	void ModelLibrary::cleanup() {

		for (const auto& entry : registry_.models) {
			entry.model->cleanup(engineDevice_, renderData_);
		}

		registry_.idByKey.clear();
		registry_.indexById.clear();
		registry_.pendingLoads.clear();
		registry_.pendingUnload.clear();
		registry_.models.clear();

		nextModelId_ = 1;

	}

}