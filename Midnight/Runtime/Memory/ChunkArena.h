#include <vector>
#include <memory>
#include <memory_resource>
namespace aveng {


	/*
	* Arena Allocator - (AKA Bump allocator or Region allocator)
	*
	*	- You allocate a large contiguous block of memory up front
	*	- All allocations inside the arena are done by bumping a pointer forward
	*	- You do not free individual allocations
	*	- You free everything at once by resetting the arena
	*
	* This allows us to avoid fragmentation (and bookkeeping!)
	* Do not use this allocator without reading the [IMPORTANT] section below.
	*
	* This means
	*	- No fragmentation
	*	- No free list
	*	- No per-object allocation
	*
	* Why?
	*	- We generate tons of temporary data
	*	- Most of it can die at the same time (chunk completion)
	*	- No fine-tuned free-ing needed
	*	- Extremely fast allocation
	*
	* Invariant:
	*	- Arena assumes all memory dies together.
	*	- no spans, no pointers, no references into to arena memory
	*	- No pmr containers referencing arena memory
	*	- monotonic_buffer_resource is non-assignable, that would invalidate it (lots of bookkeeping)
	*	- monotonic_buffer_resource is NOT thread safe
	*
	* Practical guidance for arena + tasks in your terrain pipeline:
	*	1. Choose where each stage’s memory lives
	*		- Scratch arena (reset every stage or every chunk)
	*		- Per-chunk arena (reset when chunk is destroyed/unloaded)
	*		- Persistent heap/GPU buffers
	*	2. Make resets depend on futures
	*		- "After erosion future is ready, scratch can reset"
	*		- "After mesh build future is ready and GPU upload complete, chunk arena can reset/unload”
	*	3. Avoid capturing references to temporaries
	*		- Don’t capture std::pmr::vector& that's local to the submitter unless it's guaranteed alive.
	*		- Prefer capturing raw pointers to arena-allocated structs or capturing small POD inputs by value.
	*
	* Usage 1 (w/ our threadpool) - arena-owned output:
	*
	*	struct HeightStageOutput {
	*		std::pmr::vector<float> heights;
	*	};
	*
	*	std::shared_future<HeightStageOutput*> submitHeightStage(
	*		aveng::ITaskSystem& tasks,
	*		ChunkArena& arena,
	*		inputs...
	*	) {
	*		// Allocate stage output in the arena (stable address)
	*		auto* out = std::pmr::polymorphic_allocator<HeightStageOutput>(arena.mr()).allocate(1);
	*		std::construct_at(out, HeightStageOutput{ std::pmr::vector<float>(arena.mr()) });
	*
	*		// Fill inside the task - out is the address, being copied here. Note the trailing return type
	*		return tasks.submit([out , ...inputs captured by value... ]() -> HeightStageOutput* {
	*			out->heights.resize(1024);
	*			// compute heights...
	*			return out;
	*		});
	*	}
	*
	* Usage 2 (w/ our threadpool) - arena-owned scratch + final result elsewhere
	*
	*	auto fut = tasks.submit([&arena, chunkId,  ... ]() {
	*		std::pmr::vector<float> scratch(arena.mr());
	*		scratch.resize(2048);
	*		// compute into scratch
	*		// copy/move into chunk persistent storage (NOT arena)
	*	});
	*
	* [IMPORTANT]
	* Don't repeatedly grow pmr::vector (or any container that can reallocate) in a monotonic arena.
	* In other words, avoid calls to upstream new_delete_resource() - It'll heap allocate, but I've since learned about null_memory_resource()
	*    - Instrument and record the high-water mark per chunk/stage (bytes requested),
	*    - Set bytesReserve to "typical peak + margin"
	*    - assert: if mr()->allocate() would spill, log/abort in debug builds.
	*
	* Prefer single-shot sizing (resize(n) once) or
	* Reserve(n) once, then fill without exceeding it.
	*
	* Monotonic will add padding to satisfy alignment. Usually small, but if you allocate
	* lots of tiny objects with different alignments, overhead can grow.
	* So:
	*    - allocate fewer, larger blocks (vectors/arrays) rather than many tiny allocations
	*    - prefer "struct-of-arrays-ish" buffers for big data (which we're already doing)
	*    - Don't expose partially-filled vectors to readers
	*
	* You'll notice allocation used in tandem with `call_once`. This is a great pattern here for many reasons.
	* Treat reallocations as memory leaks.
	*/

	class ChunkArena {
	public:
		ChunkArena() = default;

		explicit ChunkArena(size_t bytesReserve)
		{
			// allocate `bytesReserve` bytes up front
			reserve(bytesReserve);
		}

		void reserve(size_t bytesReserve) {
			backing_.resize(bytesReserve);
			// mono_ will now use backing_ as its buffer
			mono_ = std::make_unique<std::pmr::monotonic_buffer_resource>(
				backing_.data(),
				backing_.size(),
				std::pmr::new_delete_resource() // I believe this is the fallback resource. Could also be null_memory_resource() if we want it to throw on overflow instead of going to the heap.
			);
		}

		// This exposes the allocator (Memory Resource). Don't call before reserve()
		// usage: std::pmr::vector<T> myVec(arena.mr());
		std::pmr::memory_resource* mr() noexcept {
			return mono_.get();
		}

		// Const overload
		std::pmr::memory_resource* mr() const noexcept { 
			return mono_.get(); 
		}

		// Resets the bump pointer. All allocations are gone instantly.
		void reset() noexcept {
			if (mono_) mono_->release();
		}

	private:
		// The raw memory block used by the bump allocator
		std::vector<std::byte> backing_;
		std::unique_ptr<std::pmr::monotonic_buffer_resource> mono_;
	};


}