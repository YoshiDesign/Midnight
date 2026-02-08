#pragma once

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

	struct ChunkCoord {
		int32_t X = 0;
		int32_t Z = 0;
	};

	struct ChunkConfig {
		float ChunkSize;
		float MinPointDist;
		float HaloWidth;
		int64_t WorldSeed;
		int ChunksX;
		int ChunksZ;
	};

	struct VoronoiCell {
		SiteIndex site;
		std::vector<Triangle> triangles;
		std::vector<Vec2> vertices;
		bool closed;
	};

}