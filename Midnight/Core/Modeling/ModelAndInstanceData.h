/* separate settings file to avoid cicrula dependecies */
#pragma once
#include <variant>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <string>
#include "Core/Modeling/AssimpInstance.h"
#include "Core/Modeling/AvengInstance.h"

namespace aveng {
	// forward declaration
	class AvengModel;
	class AssimpInstance;

	// Used to identifiy model instances via InstanceSlot
	template<class Tag>
	struct InstanceHandle {
		uint32_t index = 0;
		uint32_t generation = 0;
		explicit operator bool() const { return generation != 0; } // Generation begins at 1
	};

	// Declaring this outside of the struct to symbolize their lack of functional logic
	// They carry no behavior, and don't mutate. They are simple values. Keeps InstanceHandles purely data.
	// It also decouples us from comparison semantics becoming breaking changes.
	// Non-member operators participate more cleanly in argument-dependent lookup (ADL)
	// etc.
	// C++ Guidelines: Value types should prefer non-member operators unless the operation 
	//				   conceptually modifies the object or relies on private invariants.
	template<class Tag>
	inline bool operator==(const InstanceHandle<Tag>& a, const InstanceHandle<Tag>& b) {
		return a.index == b.index && a.generation == b.generation;
	}

	template<class Tag>
	constexpr bool operator!=(const InstanceHandle<Tag>& a, const InstanceHandle<Tag>& b) noexcept {
		return !(a == b);
	}

	struct AnimatedTag {};
	using AnimatedHandle = InstanceHandle<AnimatedTag>;

	struct StaticTag {};
	using StaticHandle = InstanceHandle<StaticTag>;

	// Identified by InstanceHandle
	template <class T>
	struct InstanceSlot {
		std::optional<T> instance;
		uint32_t generation = 1;
		bool alive = false;
	};

	/* 
	* Critical invariant: 
	*	instanceSettings.isInstanceIndexPosition == index in miInstancesInOrder
	*/
	template<class Tag>
	struct ModelAndInstanceData {

		using Handle = InstanceHandle<Tag>

		// A list of the unique models currently loaded
		std::vector<std::shared_ptr<AvengModel>> miModelList{};
		int miSelectedEditorModel = 0;

		// Pools
		std::vector<InstanceSlot<Handle>> miInstanceSlots{};
		std::vector<uint32_t> miFreeIndices{}; // uint32_t slotIndex values (indices into miInstanceSlots)
		std::vector<uint32_t> miActiveIndices{}; // uint32_t slotIndex values (indices into miInstanceSlots)
		// Views / indexing
		std::vector<StaticHandle> miInstancesInOrder{};
		std::unordered_map<std::string, std::vector<Handle>> miInstancesPerModel{};

		/* pending list for deletion - outside of command buffer recording */
		std::unordered_set<std::shared_ptr<AvengModel>> miPendingDeleteAvengModels{};

		// Filepaths pending loading
		std::vector<std::string> mPendingModelLoads;

	};

	// Templated callbacks
	template<class HandleT>
	using instanceDeleteCallback = std::function<void(const HandleT&)>;
	template<class HandleT>
	using instanceCloneCallback = std::function<void(const HandleT&)>;
	template<class HandleT>
	using instanceCloneManyCallback = std::function<void(const HandleT&, int)>;
	template<class HandleT>
	using instanceCenterCallbackEditor = std::function<void(const HandleT&)>;

	// Callbacks based on model. These will validate between Anim/Static. I think
	using modelCheckCallback = std::function<bool(std::string)>;
	using modelAddCallback = std::function<bool(std::string)>;
	using modelDeleteCallback = std::function<void(std::string)>;
	using instanceAddCallback = std::function<void(std::shared_ptr<AvengModel>)>;
	using instanceAddManyCallback = std::function<void(std::shared_ptr<AvengModel>, int)>;

	struct ModelAndInstanceCallbacks {
		modelCheckCallback miModelCheckCallbackFunction;
		modelAddCallback miModelAddCallbackFunction;
		modelDeleteCallback miModelDeleteCallbackFunction;
		instanceAddCallback miInstanceAddCallbackFunction;
		instanceAddManyCallback miInstanceAddManyCallbackFunction;

		instanceDeleteCallback<AnimatedHandle> miInstanceDeleteCallbackFunction;
		instanceDeleteCallback<StaticHandle> miInstanceDeleteCallbackFunction;
		instanceCloneCallback<AnimatedHandle> miInstanceCloneCallbackFunction;
		instanceCloneCallback<StaticHandle> miInstanceCloneCallbackFunction;
		instanceCloneManyCallback<AnimatedHandle> miInstanceCloneManyCallbackFunction;
		instanceCloneManyCallback<StaticHandle> miInstanceCloneManyCallbackFunction;
		instanceCenterCallbackEditor<StaticHandle> miInstanceCenterCallbackFunction;
		instanceCenterCallbackEditor<AnimatedHandle> miInstanceCenterCallbackFunction;
	};

}