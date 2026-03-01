#pragma once
#include <string>
#include "CoreVK/Resources/wyhash.h"
#include "Core/Math/Vector.h"
#include "Module/Procgen/Noise/Config.h"

#define MIDNIGHT_WYHASH

namespace aveng {

	using EdgeIndex = uint32_t;
	using TriIndex = uint32_t;
	using SiteIndex = uint32_t;
	static constexpr EdgeIndex kInvalidEdge = -1; // NOTE - These wrap... bc their types are uint32_t. I oops'd, but we can scale to 64 if we really need validity at that scale.
	static constexpr TriIndex  kInvalidTri = -1;  // NOTE - So be consistently aware of this subtle alarm bell.

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
		uint32_t tri;    // face index
		uint32_t next;
		uint32_t twin;
	};

	// ---------- Temporary adjacency triangle (scratch) ----------
	struct AdjTri {
		SiteIndex a, b, c;      // vertex indices into allPts
		TriIndex  n0, n1, n2;   // neighbors across edges (b-c), (c-a), (a-b)
		bool      alive;
	};

	// Used to speed up barycentric computations by caching invariant values per triangle.
	// Member of the Triangulation ChunkRecord product.
	// AoS feels suitable here for now.
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

	// -------------------------
	// Bounds2
	// -------------------------
	struct Bounds2 {
		float minX{}, minZ{}, maxX{}, maxZ{};
		bool contains(float x, float z) const noexcept {
			return x >= minX && x < maxX && z >= minZ && z < maxZ;
		}
	};

#ifdef MIDNIGHT_WYHASH
	// Utility: chunk seed
	inline uint64_t packChunkCoord(ChunkCoord c) {
		return (uint64_t(uint32_t(c.x)) << 32) | uint64_t(uint32_t(c.z));
	}

	inline uint64_t mix64(uint64_t a, uint64_t b) {
		return wyhash64(a, b); // from wyhash
	}

	inline uint64_t chunkSeed(uint64_t worldSeed, ChunkCoord c) {
		return mix64(worldSeed, packChunkCoord(c));
	}
#else
	inline uint64_t chunkSeed(uint64_t worldSeed, ChunkCoord c) {
		// simple 64-bit mix
		uint64_t x = (uint32_t)c.x;
		uint64_t z = (uint32_t)c.z;
		uint64_t h = worldSeed ^ (x * 0x9E3779B185EBCA87ULL) ^ (z * 0xC2B2AE3D27D4EB4FULL);
		h ^= (h >> 30); h *= 0xBF58476D1CE4E5B9ULL;
		h ^= (h >> 27); h *= 0x94D049BB133111EBULL;
		h ^= (h >> 31);
		return h;
	}
#endif

	//inline uint64_t chunkCoordSalt() {
	//    static const uint64_t salt = /* random once */ 0x...;
	//    return salt;
	//}
	//uint64_t h = mix64(chunkCoordSalt(), packChunkCoord(c));

	// TODO - Template this, it's the same as QKeyHash
	// Note: A weak hash often has poor low-bit behavior. This would result in less 
	// bucket utilization for our striped mutex map.
	struct ChunkCoordHash {
		size_t operator()(ChunkCoord const& c) const noexcept {
			// No need to include worldSeed here; this is just a key hash.
			// If you want DoS resistance, add a process-random salt.
			uint64_t h = mix64(0, packChunkCoord(c)); // uses wyhash at the moment
			return size_t(h);
		}
	};

	/*
	* The ChunkManager makes use of each of the stage managers.
	* It owns a TerrainConfig, while each of the other managers
	* Have their own "Params" struct.
	*/

	/* Global Terrain Config - Used by the ChunkManager to orchestrate and define chunk generation */
	struct TerrainConfig {
		uint64_t worldSeed = 42;
		float chunkSize = 256.f;	// This determines the resolution of our chunks
		float minPointDist = 8.f;	// Min distance between points - This number has a large influence on the perf of BlueNoise
		float halo = 32.f;			// 4x minPointDist, for now
		uint16_t nThreads = 0;
		noise::NoiseParams noise{};
		bool hydraulicErosionEnabled = true;
		bool thermalErosionEnabled = true;
		bool ridgeEnhancementEnabled = true;
		bool hardnessMapEnabled = true;
	};

	/* Stage Params 
	 * [IMPORTANT] Stage params set a hard limit on parallelism
	 * by reading ITaskSystem::nThreads. Set this or else.
	 * TODO - Remove defaults from this declaration
	 */
	struct HydraulicErosionParams{
		uint32_t numDroplets;   // total droplets
		uint32_t maxSteps;		// steps per droplet (upper bound)
		uint32_t batchSize;		// droplets per task
		uint16_t maxWorkers;	// Dont forget to set this based on TerrainConfig::nThreads !!!!

		float inertia;		   // Per the whitepaper
		float gravity;		   // Per the whitepaper
		float pCapacity;	   // Per the whitepaper
		float pMinSlope;	   // Per the whitepaper
		float pDeposition;	   // Per the whitepaper
		float pErosion;		   // Per the whitepaper
		float pEvaporation;	   // Per the whitepaper

		float spawnMargin;		// Drops don't land in the margin (in world space)

		// extra evaporation in flats (this is my own addition, to save on compute)
		float flatSlopeEps;		// threshold for "close to 0" slope
		float flatCapEps;		// threshold for "nearly 0" capacity
		float flatExtraEvap;	// additional evaporation factor when flat+low-cap

		// droplet initial state
		float initWater;
		float initVel;
	};

	struct ThermalErosionParams {
		float TalusThreshold;	// degrees angle of repose
		float TransferRate;		// Transfer % of excess per iteration
		size_t Iterations;
		uint16_t maxWorkers;
	};

	struct HardnessParams {
		float ElevationWeight; // How much elevation affects hardness (0-1)
		float NoiseWeight;	   // How much noise variation to add (0-1)
		float NoiseFrequency;  // Frequency of hardness noise (lower = larger features)
		float BaseHardness;	   // Minimum hardness for all sites (0-1)
		float ElevationPower;  // Exponent for elevation curve (1=linear, 2=quadratic)
		bool Enabled;		   // Master toggle for hardness map generation
	};

	struct RidgeParams {
		float Threshold;     // Minimum ridgeness score to enhance (0-1)
		float BoostAmount;   // Maximum height boost in world units
		float NoiseAmount;   // Jaggedness noise amplitude
		float NoiseFreq;     // Jaggedness noise frequency
		int Iterations;		 // Number of enhancement passes
		float MinHeight;     // Minimum elevation for ridge enhancement (world units)
		std::string MinHeightMode; // "absolute" = world height, "normalized" = 0-1 within chunk
		uint16_t MaxWorkers;
		bool Enabled;
	};

	/* Stage Settings */
	struct ErosionSettings {
		HydraulicErosionParams hydraulic{};
		ThermalErosionParams thermal{};
		HardnessParams hardness{};
		RidgeParams ridges{};
	};
}