#pragma once
#include "Core/Math/Vector.h"
#include "Module/Procgen/Types2.h"
#include "Module/Procgen/TerrainArena.h"

namespace procgen {

	const uint32_t INVALID_CHUNK_INDEX = 0xfffffffe;
	const uint64_t INVALID_CHUNK_REQUEST = 0xfffffefe;


	enum class RenderableBuildState : uint8_t
	{
		None,
		Queued,
		Building,
		Ready,
		Failed
	};

	enum class StageState : uint8_t
	{
		NotStarted,
		Queued,
		Running,
		Ready,
		Failed
	};

	struct Points {
		aveng::Vec2* core{};
		uint32_t size_core{};

		// same backing storage as core, but reordered into contiguous regions
		uint32_t begin_northWest{}, count_northWest{};
		uint32_t begin_north{}, count_north{};
		uint32_t begin_northEast{}, count_northEast{};
		uint32_t begin_west{}, count_west{};
		uint32_t begin_center{}, count_center{};
		uint32_t begin_east{}, count_east{};
		uint32_t begin_southWest{}, count_southWest{};
		uint32_t begin_south{}, count_south{};
		uint32_t begin_southEast{}, count_southEast{};
	};

	struct AllPoints {
		aveng::Vec2*  all_pts;     // core + halo
		uint32_t size_all_pts;

		uint32_t*     coreIdx; // indices into pts that are core
		uint32_t size_cordIdx;
	};

	struct HeightField {
		float*        heights; // parallel with AllPoints::pts
		uint32_t size_heights;
	};

	struct Triangulation {
		Triangle*     tris;
		uint32_t size_tris;

		HalfEdge*     halfEdges;
		uint32_t size_halfEdges;

		TriangleCache* cache;
		uint32_t  size_cache;

		aveng::Vec2*  circumcenters;
		uint32_t size_circumcenters;

		// Accelerators - Used by nature sim's quite a lot.
		EdgeIndex*    triEdge0;  // size = tris.size()
		uint32_t size_triEdge0;

		EdgeIndex*    siteEdge;  // size = vertexCount (allPoints count. core + halo)
		uint32_t size_siteEdge;
	};

	struct ErosionField {
		float*        eHeights; // parallel with AllPoints::all_pts
		uint32_t size_eHeights;
	};

	/**
	 * One of the challenges of our V2 architecture is to keep the chunk record trivial to copy.
	 * This allows for more efficient memory management and easier serialization. (E.g. Pop n' Swap)
	 */

	struct ChunkRecord2 {
		bool			active = false; 
		uint32_t		generation = 0;
		uint32_t		index = INVALID_CHUNK_INDEX;
		ChunkCoord		coord{};
		Bounds2			coreBounds{};
		float			halo = 32.f;
		uint64_t		chunkSeed = 42;

		ScratchArena*	scratch = nullptr;	// Pointer to the TerrainPool scratch segment for this record
		ScratchArena*	final = nullptr;	// Pointer to the TerrainPool final segment for this record

		// Pointers into final segment 
		Points*			points = nullptr;
		AllPoints*		allPoints = nullptr;
		HeightField*	heightField = nullptr;
		Triangulation*	triangulation = nullptr;
		ErosionField*	erosion = nullptr;

		// Streaming / residency - This will be built into a map on the ChunkManager
		//int32_t pinCount{ 0 };
		//uint64_t lastTouchedFrame{ 0 };

	};

}