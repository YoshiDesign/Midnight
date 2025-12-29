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

	// Used to identifiy model instances via InstanceSlot
	template<class Tag>
	struct InstanceHandle {
		uint32_t index = 0;
		uint32_t generation = 0;
		explicit operator bool() const { return generation != 0; } // Generation begins at 1
	};

	/*
	 * These two tags allow us to safely use typed instances of InstanceManager(or whoever needs em)
	 * We're creating 2 unique type-aliases to satisfy a specific template type for ModelAndInstanceCallbacks.
	 * In short: Give InstanceManager a way to differentiate its internal types
	 */
	struct AnimatedTag {}; // 1 These two types are used to define the template on `InstanceManager<AnimatedTag>`
	using AnimatedHandle = InstanceHandle<AnimatedTag>;

	struct StaticTag {}; // 2 These two types are used to define the template on `InstanceManager<StaticTag>`
	using StaticHandle = InstanceHandle<StaticTag>;

	/* Traits for anything that needs to differentiate mid-method/def. See: InstanceManager */
	template<class Tag> struct TagTraits;
	template<> struct TagTraits<StaticTag> { static constexpr bool kAnimated = false; };
	template<> struct TagTraits<AnimatedTag> { static constexpr bool kAnimated = true; };

	template<class... Handles>
	using AnyHandle = std::variant<std::monostate, Handles...>;			// Reusable variant pattern 
	using AnyInstanceHandle = AnyHandle<StaticHandle, AnimatedHandle>;	// Add handles here as we create more flavors

	// Handle validation helpers - TODO audit their usage
	inline bool isValid(const AnyInstanceHandle& h) {
		return !std::holds_alternative<std::monostate>(h);
	}
	inline void clear(AnyInstanceHandle& h) { h = std::monostate{}; }

   /*
	* This comprises a strategy for creating a templated type alias in `InstanceManager`
	* So we can have a type of simply `using Instance = InstanceFor<Tag>;`
	*/
	template<class Tag>
	struct InstanceTypeFor; // primary template

	template<>
	struct InstanceTypeFor<StaticTag> {
		using instance_type = AvengInstance;
		using create_settings = TransformSettings; // Just the transform settings
		static constexpr bool kAnimated = false;
	};

	template<>
	struct InstanceTypeFor<AnimatedTag> {
		using instance_type = AssimpInstance;
		using create_settings = AnimatedCreateSettings; // Both the transform and animation settings
		static constexpr bool kAnimated = true;
	};

	// A readability wrapper.
	template<class Tag>
	using InstanceFor = typename InstanceTypeFor<Tag>::instance_type; // `::instance_type` extracts the `using instance_type` value
	// `typename` is required here because the compiler cannot know if ::instance_type is a type or a value at compile time.
	// Fun fact: (example) `AddPointer<int>::type` was the norm before `using` hit the scene in C++11
	/* */ /* */ /* */
	
	template<class Tag>
	using CreateSettingsFor = typename InstanceTypeFor<Tag>::create_settings;

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
	// The above operator overloads allow us to do do things like:
	// vec.erase(std::remove(vec.begin(), vec.end(), handle), vec.end());

	// Important: Tag is not required here, but it prevents accidental cross-pool misuse. The compiler will catch it.
	template <class InstanceT>
	struct InstanceSlot {
		std::optional<InstanceT> instance;
		uint32_t generation = 1;
		bool alive = false; //  invariant: if alive -> instance.hasValue() == true
	};

	template<class Tag>
	struct InstancePoolData {
		using Handle = InstanceHandle<Tag>;
		using Instance = InstanceFor<Tag>;
		using Slot = InstanceSlot<Instance>;

		std::vector<Slot> slots{};		// Access to the instance and its generation / alive status
		std::vector<uint32_t> free{};	// Indices of available `slots`
		std::vector<uint32_t> active{};	// Indices of active `slots`

		std::vector<Handle> instancesInOrder{};
		std::unordered_map<ModelId, std::vector<Handle>> instancesPerModel;

		
	};

	// Templated callbacks - Instances
	template<class HandleT>
	using instanceAddCallback = std::function<void(const ModelRef&)>;
	template<class HandleT>
	using instanceAddManyCallback = std::function<void(const ModelRef&, std::span<const InstanceSettings>, unsigned int )>;
	template<class HandleT>
	using instanceDeleteCallback = std::function<void(const HandleT&)>;
	template<class HandleT>
	using instanceDeleteManyCallback = std::function<void(std::span<const HandleT>)>;
	template<class HandleT>
	using instanceCloneCallback = std::function<void(const HandleT&)>;
	template<class HandleT>
	using instanceCloneManyCallback = std::function<void(const HandleT&, unsigned int)>;
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

	// This goes to Renderer
	struct ModelCallbacks {
		modelCheckCallback  onModelCheck;
		modelAddCallback    onModelAdd;
		modelDeleteCallback onModelDelete;

		// This probably doesn't need to be in here
		//InstanceCallbacksPerPool<AnimatedHandle> anim;
		//InstanceCallbacksPerPool<StaticHandle>   stat;
	};

}