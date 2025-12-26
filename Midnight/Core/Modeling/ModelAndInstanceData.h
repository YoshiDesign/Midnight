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
#include "Core/Modeling/Sources/IModelSource.h"

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

	// These two tags allow us to safely use typed instances of InstanceManager (or whoever needs em)
	// We're creating 2 unique type-aliases to satisfy a specific template type for ModelAndInstanceCallbacks
	struct AnimatedTag {};
	using AnimatedHandle = InstanceHandle<AnimatedTag>;

	struct StaticTag {};
	using StaticHandle = InstanceHandle<StaticTag>;

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

	// Important: Tag is not required here, but it prevents accidental cross-pool misuse. The compiler will catch it.
	template <class Tag/*phantom type*/, class InstanceT>
	struct InstanceSlot {
		std::optional<InstanceT> instance;
		uint32_t generation = 1;
		bool alive = false;
	};

	/* 
	* TODO: refactoring- 
	*	instanceSettings.isInstanceIndexPosition == index in miInstancesInOrder
	* Audit this prior relationship
	*/

	struct ModelRegistryData {
		std::vector<ModelEntry> models;
		std::unordered_map<AssetKey, ModelId> idByKey;
		std::unordered_map<ModelId, size_t> indexById;

		std::vector<AssetKey> pendingLoads;
		std::vector<ModelId> pendingUnload;

		std::optional<ModelId> selectedModel;
	};

	template<class Tag>
	struct InstancePoolData {
		using Handle = InstanceHandle<Tag>;
		using Slot = InstanceSlot<typename InstanceTypeFor<Tag>::type>;

		std::vector<Slot> slots{};
		std::vector<uint32_t> free{};
		std::vector<uint32_t> active{};

		std::vector<Handle> instancesInOrder{};
		std::unordered_map<ModelId, std::vector<Handle>> instancesPerModel;

		int selectedEditorInstance = 0;
	};

	// Templated callbacks - Instances
	template<class HandleT>
	using instanceAddCallback = std::function<void(const ModelId)>;
	template<class HandleT>
	using instanceAddManyCallback = std::function<void(std::vector<const ModelId&> h, unsigned int n)>;
	template<class HandleT>
	using instanceDeleteCallback = std::function<void(const HandleT&)>;
	template<class HandleT>
	using instanceDeleteManyCallback = std::function<void(std::vector<const HandleT&> v)>;
	template<class HandleT>
	using instanceCloneCallback = std::function<void(const HandleT&)>;
	template<class HandleT>
	using instanceCloneManyCallback = std::function<void(const HandleT&, int)>;
	template<class HandleT>
	using instanceCenterCallbackEditor = std::function<void(const HandleT&)>;

	// Model Callbacks
	using modelCheckCallback = std::function<bool(std::string)>;
	using modelAddCallback = std::function<bool(std::string)>;
	using modelDeleteCallback = std::function<void(std::string)>;

	template<class HandleT>
	struct InstanceCallbacksPerPool {
		
		instanceDeleteCallback<HandleT>     onDelete;
		instanceDeleteManyCallback<HandleT> onDeleteMany;
		instanceCloneCallback<HandleT>      onClone;
		instanceCloneManyCallback<HandleT>  onCloneMany;
		instanceCenterCallbackEditor<HandleT>	onCenter;
		instanceAddCallback<HandleT>			onInstanceAdd;
		instanceAddManyCallback<HandleT>		onInstanceAddMany;
	};

	// This goes to Midnight
	struct ModelAndInstanceCallbacks {
		modelCheckCallback  onModelCheck;
		modelAddCallback    onModelAdd;
		modelDeleteCallback onModelDelete;

		InstanceCallbacksPerPool<AnimatedHandle> anim;
		InstanceCallbacksPerPool<StaticHandle>   stat;
	};

}