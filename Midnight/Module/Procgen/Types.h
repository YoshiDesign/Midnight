#pragma once
#include <mutex>
#include <future>
#include <memory_resource>

#include "Module/Procgen/Noise/Config.h"
#include "Core/Math/Vector.h"
#include "Utils/glm_includes.h"

namespace aveng {

	using SiteIndex = int32_t;
	using BorderIndex = int32_t;
	using GridIndex = int32_t;	// Spatial Grid Index
	using TriangleIndex = int32_t; //

	const enum Border {
		Border_None = 0,
		Border_North = 1,
		Border_South = 2,
		Border_East = 3,
		Border_West = 4,
	};

	// -------------------------
	// ChunkCoord + Hash
	// -------------------------
	struct ChunkCoord { int32_t x{}, z{}; };

	inline bool operator==(ChunkCoord a, ChunkCoord b) noexcept { return a.x == b.x && a.z == b.z; }

	// TODO - Template this, it's the same as QKeyHash
	// Note: A weak hash often has poor low-bit behavior. This would result in less 
	// bucket utilization for our striped mutex map.
	struct ChunkCoordHash {
		size_t operator()(ChunkCoord const& c) const noexcept {
			uint64_t ux = (uint32_t)c.x;
			uint64_t uz = (uint32_t)c.z;
			uint64_t h = (ux << 32) ^ uz;

			// MurmurHash3 avalanche - ensures good distribution of low bits
			h ^= (h >> 33); h *= 0xff51afd7ed558ccdULL;
			h ^= (h >> 33); h *= 0xc4ceb9fe1a85ec53ULL;
			h ^= (h >> 33);
			return (size_t)h;
		}
	};

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
	* In other words, avoid calls to upstream new_delete_resource()
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
				std::pmr::new_delete_resource()
			);
		}

		// This exposes the allocator (Memory Resource)
		// usage: std::pmr::vector<T> myVec(arena.mr());
		std::pmr::memory_resource* mr() noexcept {
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

	// -------------------------
	// Bounds2
	// -------------------------
	struct Bounds2 {
		float minX{}, minZ{}, maxX{}, maxZ{};
		bool contains(float x, float z) const noexcept {
			return x >= minX && x < maxX && z >= minZ && z < maxZ;
		}
	};

	/*
	* PMR usage -
	* We explicitly construct in order to use our arena memory resource,
	* otherwise the allocator chooses the default resource (heap) and we lose the benefits of arena allocation.
	* 
	* General usage pattern:
	*	> auto alloc = std::pmr::polymorphic_allocator<Heights>(arena.mr());
	*	> Heights* heights = alloc.allocate(1);
	*	> std::construct_at(heights, arena.mr()); // calls Heights(mr)
	* 
	* Equivalent (more engine-esque):
	*	> Heights* heights =
	*	>	static_cast<Heights*>(arena.mr()->allocate(sizeof(Heights), alignof(Heights)));
	*	> new (heights) Heights(arena.mr());
	* 
	* In the future we could create an allocator API based on our arena
	* that works with generic containters and templated systems.
	* 
	* Future-proofing for non-trivial allocators - Today you’re using monotonic_buffer_resource.
	* But imagine:
	*	A resource that tracks allocations.
	*	A resource that enforces alignment rules.
	*	A resource that injects guards.
	*	A resource that allocates from GPU-visible memory.
	*	A resource that logs.
	*	
	*	Allocator-based construction lets you swap those cleanly (general usage pattern).
	*	
	*	Placement-new hardcodes assumptions (the "equivalent" example).
	* 
	* Working example:
	* > std::shared_future<Heights*> submitHeightPass(
	* >		aveng::ITaskSystem& tasks,
    * >		ChunkArena& arena,
	* >		size_t pointCount // from AllPoints::pts size
	* >	)
	* >	{
	* >		// 1) Allocate Heights object in arena and construct it with mr
	* >		auto alloc = std::pmr::polymorphic_allocator<Heights>(arena.mr());
	* >		Heights* out = alloc.allocate(1);
	* >		std::construct_at(out, arena.mr());
	* >
	* >		// 2) Enqueue job that fills it
	* >		return tasks.submit([out, pointCount]() -> Heights* {
	* >			out->h.resize(pointCount);
	* >			// compute out->h[i] ...
	* >			return out;
	* >		});
	* >	}
	*/

	// -------------------------
	// Products (pmr containers)
	// These are each effectively:
	//	pointer
	//	size
	//  capacity
	// No different than a typical container, but we construct them with a memory resource that belongs to an arena.
	// 
	// Be aware, with a monotonic_buffer_resource: 
	//  When vector grows, it allocates new memory 
	//  Old memory is NOT freed.
	// So repeated resize() or reserve() growth patterns may accumulate memory inside the arena
	// -------------------------
	struct Points {
		std::pmr::vector<Vec2> core; // core points only
		explicit Points(std::pmr::memory_resource* mr) : core(mr) {}
	};

	struct AllPoints {
		std::pmr::vector<Vec2>  pts;     // core + halo
		std::pmr::vector<uint32_t>   coreIdx; // indices into pts that are core
		explicit AllPoints(std::pmr::memory_resource* mr) : pts(mr), coreIdx(mr) {}
	};

	struct Heights {
		std::pmr::vector<float> h; // same length as AllPoints::pts
		explicit Heights(std::pmr::memory_resource* mr) : h(mr) {}
	};

	struct Triangulation {
		std::pmr::vector<glm::uvec3> tris;        // indices into AllPoints::pts
		std::pmr::vector<glm::vec2>  circumcenters; // optional
		explicit Triangulation(std::pmr::memory_resource* mr) : tris(mr), circumcenters(mr) {}
	};

	// Placeholder (not designing hydrology now)
	struct ErosionField {
		std::pmr::vector<float> hPost; // heights after erosion, same indexing as AllPoints
		explicit ErosionField(std::pmr::memory_resource* mr) : hPost(mr) {}
	};

	// Final durable product example (you’ll extend)
	struct FinalMeshCPU {
		std::pmr::vector<glm::vec3> positions;
		std::pmr::vector<glm::vec3> normals;   // could be filled by compute later
		std::pmr::vector<uint32_t>  indices;
		explicit FinalMeshCPU(std::pmr::memory_resource* mr)
			: positions(mr), normals(mr), indices(mr) {
		}
	};

	struct ChunkRecord {

		/*
		 * NOTE: This code was generated under these very open-ended assumptions:
		 *	1. you want each stage to be a "published product" inside the chunk record, not necessarily outside the chunk,
		 *	2. and you want a uniform pattern so that adding/removing stages doesn’t change concurrency wiring style.
		 * This makes the procedural generation pipeline very easy to extend while in development.
		 */

		ChunkCoord coord{};
		Bounds2 coreBounds{};
		float halo = 0.f;

		// Arena strategy:
		// - scratch: intermediates, reset when you no longer need them
		// - final: durable outputs (mesh + gameplay outputs)
		ChunkArena scratch; // Tier 2 of our 3-tier arena strategy - For intermediate results that other stages may depend on
		ChunkArena final; // Tier 3 of our 3-tier arena strategy - For final results that are used by the simulation runtime

		// Products live inside arenas (allocated with pmr containers).
		// We store raw pointers because arenas own the memory; record lifetime owns arenas.
		Points* points = nullptr;
		AllPoints* allPoints = nullptr;
		Heights* heights = nullptr;
		Triangulation* triangulation = nullptr;
		ErosionField* erosion = nullptr;
		FinalMeshCPU* finalMesh = nullptr;

		// Stage futures
		std::once_flag pointsOnce;
		std::shared_future<Points const*> pointsF;

		std::once_flag allPointsOnce;
		std::shared_future<AllPoints const*> allPointsF;

		std::once_flag heightsOnce;
		std::shared_future<Heights const*> heightsF;

		std::once_flag triangOnce;
		std::shared_future<Triangulation const*> triangF;

		std::once_flag erosionOnce;
		std::shared_future<ErosionField const*> erosionF;

		std::once_flag meshOnce;
		std::shared_future<FinalMeshCPU const*> meshF;

		// Streaming / residency
		std::atomic<int32_t> pinCount{ 0 };
		std::atomic<uint64_t> lastTouchedFrame{ 0 };

		/*
		* Hard Invariants:
		*	Scratch outputs: not published (or published only as a completion signal), or published as "handle + owner"
		*	Final outputs: safe to publish as raw pointers because they’re stable until eviction
		*
		* Why?
		*	`discardScratchIntermediates()` resets the arena and nulls pointers, but:
		*	the futures may still exist and be shared elsewhere
		*	shared_future<Points const*> might still return a pointer that now points into freed arena memory
		*
		*	Be sure to clearly delineate between what is an internal dependency and what is a public artifact.
		*	Do not let this become a lifetime safety nightmare by resetting scratch when futures still exist.
		*/

		// discardScratchIntermediates Policy: after mesh is built, you can drop intermediates.
		/**
		 * This is our solution to: 
		 *  - Task A publishes Heights* from scratch arena
		 *	- Task B gets future, hasn't called .get() yet
		 *	- Task C calls rec.scratch.reset()
		 *	- Task B calls fut.get() -> dangling pointer
		 * I've probably said this elsewhere, but just in case:
		 *  Only reset scratch after:
         *  - Final mesh is complete (all intermediate futures resolved)
         *  - No external references to intermediate data exist
		 */
		void discardScratchIntermediates() {
			// reset scratch arena; product pointers become invalid, so null them
			scratch.reset();
			points = nullptr;
			allPoints = nullptr;
			heights = nullptr;
			triangulation = nullptr;
			erosion = nullptr;
		}
	};

	struct TerrainConfig {
		uint64_t worldSeed = 42;
		float chunkSize = 256.f;
		float minPointDist = 8.f;
		float halo = 32.f;   // consider 4x minPointDist as a starting point
		NoiseParams noise{};
	};

	struct Site {
		Vec2 Pos;
		float Height;
	};

	struct Triangle {
		SiteIndex A, B, C;
	};

	struct HalfEdge {
		SiteIndex Origin;
		SiteIndex EdgeDest;
		int Tri;
		int Next;
		int Twin;
		int Prev;
	};

	struct ChunkConfig {
		uint64_t worldSeed = 0;
		float chunkSize = 256.f;
		float minPointDist = 8.f;
		float halo = 32.f;   // consider 4x minPointDist as a starting point
		NoiseParams noise{};
		int chunksX;
		int chunksZ;
	};

	struct VoronoiCell {
		SiteIndex site;
		std::vector<Triangle> triangles;
		std::vector<Vec2> vertices;
		bool closed;
	};

}