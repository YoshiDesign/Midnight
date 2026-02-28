#pragma once
#include <memory_resource>
#include <future>
#include <mutex>
#include <optional>
#include <cstdint>
#include "Utils/glm_includes.h"
#include "Module/Procgen/Types.h"
#include "Module/Procgen/SpatialGrid.h"
#include "Runtime/Memory/ChunkArena.h"
namespace aveng {
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

	// The structs below represent the final products of each stage.
	// -------------------------
	// Products (pmr containers) - Construct with arena memory resource
	// These are each effectively:
	//	pointer
	//	size
	//  capacity
	// 
	// Be aware, with a monotonic_buffer_resource: 
	//  When vector grows, it allocates new memory 
	//  Old memory is not freed.
	// So repeated resize() or reserve() growth patterns may accumulate memory inside the arena.
	// For this reason we always compute our resources in a scratch pmr, resize, and set the final product once.
	// -------------------------
	struct Points {
		std::pmr::vector<Vec2> core; // core points only
		explicit Points(std::pmr::memory_resource* mr) : core(mr) {}
	};

	struct AllPoints {
		std::pmr::vector<Vec2> pts;     // core + halo
		std::pmr::vector<uint32_t> coreIdx; // indices into pts that are core
		explicit AllPoints(std::pmr::memory_resource* mr) : pts(mr), coreIdx(mr) {}
	};

	struct HeightField {
		std::pmr::vector<float> heights; // parallel with AllPoints::pts
		explicit HeightField(std::pmr::memory_resource* mr) : heights(mr) {}
	};

	struct Triangulation {
		std::pmr::vector<Triangle>      tris;
		std::pmr::vector<HalfEdge>      halfEdges;
		std::pmr::vector<TriangleCache> cache;
		std::pmr::vector<Vec2>          circumcenters;

		// Accelerators - Used by nature sim's quite a lot.
		std::pmr::vector<EdgeIndex>     triEdge0;  // size = tris.size()
		std::pmr::vector<EdgeIndex>     siteEdge;  // size = vertexCount (allPoints count. core + halo)

		explicit Triangulation(std::pmr::memory_resource* mr)
			: tris(mr), halfEdges(mr), cache(mr), circumcenters(mr), triEdge0(mr), siteEdge(mr) {
		}
	};

	// Example...
	struct ErosionField {
		std::pmr::vector<float> eHeights; // parallel with AllPoints::pts
		explicit ErosionField(std::pmr::memory_resource* mr) : eHeights(mr) {}
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

	//
	struct VoronoiCell {
		SiteIndex site = -1;
		bool closed = false;
		std::pmr::vector<TriIndex> tris;
		std::pmr::vector<Vec2> vertices;

		explicit VoronoiCell(std::pmr::memory_resource* mr)
			: tris(mr), vertices(mr) {
		}
	};

	// A registry of stage products + futures + residency
	struct ChunkRecord {

		/*
		 * NOTE: This code was generated under these very open-ended assumptions:
		 *	1. you want each stage to be a "published product" inside the chunk record, not necessarily outside the chunk,
		 *	2. and you want a uniform pattern so that adding/removing stages doesn’t change concurrency wiring style.
		 * This makes the procedural generation pipeline very easy to extend while in development.
		 */

		ChunkCoord coord{};
		Bounds2 coreBounds{}; // World space bounds for the chunk area
		float halo = 0.f;
		uint64_t chunkSeed = 0;

		// Arena strategy:
		// - (T2) scratch: intermediates, reset when you no longer need them
		// - (T3) final: durable outputs (mesh + gameplay outputs)
		ChunkArena final;	// Tier 3
		ChunkArena scratch; // Tier 2
		// Tier 1 is thread-local scratch allocation

// Arenas own the memory; record lifetime owns arenas.
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

		std::once_flag spatialOnce;
		std::shared_future<SpatialGrid const*> spatialF;

		// Note - SpatialGrid bounds are core + halo
		std::optional<SpatialGrid> spatial; // Not trivially destructible!
											// This must also remain moveable due to its usage in the StripeBucket. 

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
			heightField = nullptr;
			triangulation = nullptr;
			erosion = nullptr;
		}
	};

}
