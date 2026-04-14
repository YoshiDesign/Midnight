#pragma once
#include "Core/Math/Vector.h"
#include "Module/Procgen/Types2.h"

namespace procgen {

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
		aveng::Vec2*  core; // core points only
		uint32_t size_core;
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
		ChunkCoord     coord{};
		Bounds2   coreBounds{};
		float       halo = 0.f;
		uint64_t chunkSeed = 0;

		// Maybe - 
		uint32_t packed_offset;

		// Pointers into final Arena memory
		Points* points = nullptr;
		AllPoints* allPoints = nullptr;
		HeightField* heightField = nullptr;
		Triangulation* triangulation = nullptr;
		ErosionField* erosion = nullptr;

		// Streaming / residency
		int32_t pinCount{ 0 };
		uint64_t lastTouchedFrame{ 0 };

	};

}