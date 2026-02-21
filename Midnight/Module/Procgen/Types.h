#pragma once
#include <mutex>
#include <future>
#include <memory_resource>
#include "Utils/glm_includes.h"
#include "Module/Procgen/Noise/Config.h"
#include "Core/Math/Vector.h"
#include "Runtime/Memory/ChunkArena.h"

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

	struct HeightField {
		std::pmr::vector<float> heights; // same length as AllPoints::pts
		explicit HeightField(std::pmr::memory_resource* mr) : heights(mr) {}
	};

	struct Triangulation {
		std::pmr::vector<Triangle> tris;        // indices into AllPoints::pts
		std::pmr::vector<Vec2>  circumcenters; // optional
		explicit Triangulation(std::pmr::memory_resource* mr) : tris(mr), circumcenters(mr) {}
	};

	// Placeholder (not designing hydrology now)
	struct ErosionField {
		std::pmr::vector<float> hDelta; // heights after erosion, same indexing as AllPoints
		explicit ErosionField(std::pmr::memory_resource* mr) : hDelta(mr) {}
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
		// Tier 1 is thread-local allocation

		// Products live inside arenas (allocated with pmr containers).
		// We store raw pointers because arenas own the memory; record lifetime owns arenas.
		Points* points = nullptr;
		AllPoints* allPoints = nullptr;
		HeightField* heightField = nullptr;
		Triangulation* triangulation = nullptr;
		ErosionField* erosion = nullptr;
		FinalMeshCPU* finalMesh = nullptr;

		// Stage futures
		std::once_flag pointsOnce;
		std::shared_future<Points const*> pointsF;

		std::once_flag allPointsOnce;
		std::shared_future<AllPoints const*> allPointsF;

		std::once_flag heightsOnce;
		std::shared_future<HeightField const*> heightsF;

		std::once_flag triangOnce;
		std::shared_future<Triangulation const*> triangF;

		std::once_flag erosionOnce;
		std::shared_future<ErosionField const*> erosionF;

		std::once_flag meshOnce;
		std::shared_future<FinalMeshCPU const*> meshF;

		// Streaming / residency
		std::atomic<int32_t> pinCount{ 0 };
		std::atomic<uint64_t> lastTouchedFrame{ 0 };

		std::vector<Site> Sites;
		std::vector<Triangle> Tris;
		std::vector<HalfEdge> HalfEdges;
		std::vector<Vec3> FaceNormals;

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
			heightField = nullptr;
			triangulation = nullptr;
			erosion = nullptr;
		}
	};

	struct TerrainConfig {
		uint64_t worldSeed = 42;
		float chunkSize = 256.f;
		float minPointDist = 8.f;
		float halo = 32.f;   // consider 4x minPointDist as a starting point
		noise::NoiseParams noise{};
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

	//struct ChunkConfig {
	//	uint64_t worldSeed = 0;
	//	float chunkSize = 256.f;
	//	float minPointDist = 8.f;
	//	float halo = 32.f;   // consider 4x minPointDist as a starting point
	//	NoiseParams noise{};
	//	int chunksX;
	//	int chunksZ;
	//};

	struct VoronoiCell {
		SiteIndex site;
		std::vector<Triangle> triangles;
		std::vector<Vec2> vertices;
		bool closed;
	};

}