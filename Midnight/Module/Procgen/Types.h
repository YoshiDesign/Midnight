#pragma once
#include "Core/Math/Vector.h"
#include "Module/Procgen/Noise/Config.h"

namespace aveng {

	using EdgeIndex = int32_t;
	using TriIndex = int32_t;
	using SiteIndex = uint32_t;
	static constexpr EdgeIndex kInvalidEdge = -1;
	static constexpr TriIndex  kInvalidTri = -1;

	const enum Border {
		Border_None = 0,
		Border_North = 1,
		Border_South = 2,
		Border_East = 3,
		Border_West = 4,
	};

	struct Triangle {
		SiteIndex A, B, C;
	};

	// AoS is perfectly suitable for this struct.
	// If we needed to perform linear operations over
	// thousands of half-edges, then we might consider SoA for better perf.
	// Note that we don't have a `prev` member. We just use delaunay traversal based on next/twin to do that
	struct HalfEdge {
		SiteIndex origin;
		int tri;    // face index
		int next;
		int twin;
	};

	// ---------- Temporary adjacency triangle (scratch) ----------
	struct AdjTri {
		SiteIndex a, b, c;      // vertex indices into allPts
		TriIndex  n0, n1, n2;   // neighbors across edges (b-c), (c-a), (a-b)
		bool      alive;
	};

	// Used to speed up barycentric computations by caching invariant values per triangle.
	// Member of the Triangulation ChunkRecord product.
	// AoS also feels suitable here for now.
	struct TriangleCache {
		Vec2 ab;
		Vec2 ac;
		float invDenom;
	};

	// Used to validate, identify and store triangle edges during post-triangulation phase
	struct TriEdgeDesc {
		uint64_t key;
		int slot; // 0..2
	};

	struct TriEdgeRef { TriIndex triIdx; int edgeSlot; }; // edgeSlot 0..2

	// Deprecated
	/*struct Site {
		Vec2 Pos;
		float Height;
	};*/
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

	struct TerrainConfig {
		uint64_t worldSeed = 42;
		float chunkSize = 256.f;
		float minPointDist = 8.f;
		float halo = 32.f;   // consider 4x minPointDist as a starting point
		noise::NoiseParams noise{};
	};



}