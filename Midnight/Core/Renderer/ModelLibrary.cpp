#include "ModelLibrary.h"
#include "Core/Imaging/MidnightTextureSystem.h"
#include "Core/Modeling/Sources/FilesystemAssetSource.h"
#include "Core/Modeling/Sources/PackAssetSource.h"
#include "CoreVK/EngineDevice.h"

namespace aveng {

	static AssetKey normalizeAssetKey(std::string_view in) {
		AssetKey out(in);
		for (char& c : out) {
			if (c == '\\') c = '/';
		}
		// Optional: lowercase, strip "./", etc.
		std::cout << "[ModelLibrary] Normalized Asset Key: " << out << std::endl;
		return out;
	}

	std::unique_ptr<IAssetSource> ModelLibrary::createAssetSource() {
#ifdef ENABLE_EDITOR
		return std::make_unique<FilesystemAssetSource>();
#else
		return std::make_unique<FilesystemAssetSource>();
		//return std::make_unique<PackModelSource>("assets.pak");
#endif
	}

	ModelLibrary::ModelLibrary(
		EngineDevice& engineDevice,
		VkRenderData& renderData,
		MidnightTextureSystem& textureSystem
	) : engineDevice_{ engineDevice }, renderData_{ renderData }, textureRegistry_{ textureSystem } {
		
		// Register a null model for "no selected models"
		ModelEntry entry;
		entry.id = NullModelId;
		entry.key = "null/";
		entry.model = nullptr;
		entry.isAnimated = false;
		entry.boneMeta.boneCount = 0;

		registry_.models.emplace_back(std::move(entry));
		registry_.idByKey.emplace("null", NullModelId);
		registry_.indexById.emplace(NullModelId, 0); // Lives at index 0

		// Determine model source
		assetSource_ = createAssetSource();
	}

	/* IModelLibrary Overload */
	const AvengModel* ModelLibrary::pModel(ModelId id) const {
		auto it = registry_.indexById.find(id); // Required when const qualified and working with unordered_map indices
		if (it == registry_.indexById.end()) return nullptr;

		const size_t idx = it->second;
		return registry_.models[idx].model.get();
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

		/* TODO - clean this up
				- Normalize the asset directory if loading from file source
		*/

		std::cout << "[ModelLibrary::getOrLoadModel] Loading Model: " << assetKey << std::endl;

		// Already known?
		if (auto it = registry_.idByKey.find(key); it != registry_.idByKey.end()) { // very "Go" semantic lol
			const ModelId id = it->second;
			const auto idxIt = registry_.indexById.find(id);
			if (idxIt == registry_.indexById.end()) {
				// Should never happen if maps are consistent - We had a key to no ID
				std::cout << "[ModelLibrary::getOrLoadModel] Anomaly alert [1]\n";
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
				std::cout << "[ModelLibrary::getOrLoadModel] Key found without a model - Anomaly alert [2]\n";
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
			std::cout << "Queued Model unloading: " << idx << std::endl;

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
				/// IMPORTANT: If you ever want to load or unload models mid-game, this needs to change.
				/// Do not idle the device. All you need to do is wait for 'n' frames to complete
				vkDeviceWaitIdle(engineDevice_.device());
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

				/* could also have used std::swap to get around move semantics */

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

	void ModelLibrary::processPendingModelLoads(const int frameIndex)
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
				// Key got removed or never registered (should be rare) - implies model bypassed other registry entries
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
			std::cout << "Queued Model ENtry loading: " << entry.id << std::endl;

			// Might have loaded already (if duplicate queue)
			if (entry.model) {
				std::cout << "Model already found: Abort\n ";
				continue;
			}

			// 1) Read
			std::vector<std::byte> modelBytes = assetSource_->readModelBytes(key);

			uint32_t nextBoneOffset = renderData_.skinState.nextBoneOffsetMatIdx;
			uint32_t nextParentOffset = renderData_.skinState.nextBoneParentIdxIdx;

			// 2) Build/import
			std::unique_ptr<AvengModel> model = buildModelFromSource(key, modelBytes, frameIndex);
			if (!model) {
				ejectModel(registry_.idByKey[key], key);
				std::printf("[ModelLibrary] Failed to load model: %s\n", key.c_str());
				continue;
			}

			// Commit to its registry entry
			entry.rootTransform = model->getRootTranformationMatrix();
			entry.model = std::move(model);
			entry.isAnimated = entry.model->hasAnimations();

			const uint32_t boneCount = static_cast<uint32_t>(entry.model->getBoneList().size());
			assert(entry.model->nParentNodeIndices == boneCount);

			entry.boneMeta.boneOffsetBase = nextBoneOffset;
			entry.boneMeta.boneParentBase = nextParentOffset;
			entry.boneMeta.boneCount = boneCount;

			// [IMPORTANT] Sensitive data - these are globally determining the next model's offset
			// VERY	MUCH NOT THREAD SAFE TO MULTITHREAD THE PENDING MODEL LOADS FOR THIS REASON
			renderData_.skinState.nextBoneOffsetMatIdx += boneCount;
			renderData_.skinState.nextBoneParentIdxIdx += boneCount;

			ModelSkinMeta ms{
				renderData_.skinState.nextBoneOffsetMatIdx,
				renderData_.skinState.nextBoneParentIdxIdx,
				boneCount,
				0xBEEEEEEF // there it is (std140)
			};

			// Update the ModelSkinMeta buffer



			if (entry.isAnimated) {
				std::cout << entry.key << " has " << entry.boneMeta.boneCount << " bones.\n";
			}

			anyLoaded = true;
			lastLoadedId = entry.id;

		}

		// Arbitrary
		if (anyLoaded && lastLoadedId != NullModelId) {
			// TODO - Why is this occurring here?
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
		std::span<const std::byte> bytes,
		const int frameIndex
	) {
		// Construct model
		auto model = std::make_unique<AvengModel>(engineDevice_);

		// Base directory policy for resolving external refs (textures/bin/etc)
		// Import flags�keep your existing defaults, pass extra if needed
		constexpr unsigned int extraImportFlags = 0;

		const std::string modelBaseDir = baseDirForAssetKey(key);                // e.g. "Assets/Models/House"
		const std::string engineTextureDir = normalizeAssetKey(joinPath(contentRoot_, textureRoot_)); // e.g. "Assets/textures"
		const std::string baseDir = baseDirForAssetKey(key);

		// NOTE: AvengModel now does "import/build", not IO.
		//const bool ok = model->loadModelV2(
		//	renderData_,
		//	key,
		//	bytes,
		//	extraImportFlags,
		//	modelBaseDir,
		//	engineTextureDir
		//);
		
		const bool ok = model->loadModelV3(
			renderData_,
			key,
			bytes,
			extraImportFlags,
			textureRegistry_, // To register textures as they appear embedded in models
			frameIndex,
			modelBaseDir,
			engineTextureDir
		);

		if (!ok) {
			std::printf("[Renderer] buildModelFromSource failed for key=%s\n", key.c_str());
			return {};
		}

		return model;
	}

	/* Note: Does not preserve insertion order */
	void ModelLibrary::ejectModel(ModelId id, AssetKey key) {

		auto itIdx = registry_.indexById.find(id);
		if (itIdx == registry_.indexById.end()) {
			// not found; either no-op or assert/log
			return;
		}

		const size_t idx = itIdx->second;
		const size_t last = registry_.models.size() - 1;

		if (auto itKey = registry_.idByKey.find(key); itKey != registry_.idByKey.end()) {
			if (itKey->second == id) {
				registry_.idByKey.erase(itKey);
			}
			// else: key exists but maps to a different id
		}

		assert(registry_.models[idx].key == key);

		// Remove from models with swap-erase
		if (idx != last) {
			// Move last element into the hole at idx
			registry_.models[idx] = std::move(registry_.models[last]);

			// Fix indexById for the moved model
			const ModelId movedId = registry_.models[idx].id; // ModelEntry must have .id
			registry_.indexById[movedId] = idx;
		}
		registry_.models.pop_back();

		// Remove id->index mapping for the removed model (O(1))
		registry_.indexById.erase(itIdx);
	}

	void ModelLibrary::cleanup() {

		std::cout << "CLEANING UP MODEL-LIBRARY\n";
		for (const auto& entry : registry_.models) {
			if (entry.id == NullModelId) continue;
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