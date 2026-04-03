#pragma once
#include <vector>
#include <utility>
#include <unordered_map>
#include "Module/Procgen/Types.h"

namespace procgen {

	struct ModCoord {
		int x;
		int z;

		bool operator==(const ModCoord& other) const noexcept {
			return x == other.x && z == other.z;
		}
	};

	//// Simple hash. Please inline this, o wise compiler overloard
	//struct ModCoordHash {
	//	size_t operator()(const ModCoord& c) const noexcept {
	//		size_t h1 = std::hash<int>{}(c.x);
	//		size_t h2 = std::hash<int>{}(c.z);
	//		return h1 ^ (h2 << 1);
	//	}
	//};

	//const std::unordered_map<ModCoord, aveng::ChunkCoord, ModCoordHash> lmod {
	//	/// {{player's chunk-coord location % 3}, {offset from center}}
	//	{{-1, 1}, { 1, -1}},
	//	{{ 0, 1}, { 0, -1}},
	//	{{-2, 1}, {-1, -1}},
	//	{{-1, 0}, { 1,  0}},
	//	{{ 0, 0}, { 0,  0}},
	//	{{-2, 0}, {-1,  0}},
	//	{{-1, 2}, { 1,  1}},
	//	{{ 0, 2}, { 0,  1}},
	//	{{-2, 2}, {-1,  1}},
	//	/// Row-major order (where applicable) - matches our neighborhood convention
	//	{{ 2, 1}, { 1, -1}},
	//	{{ 1, 1}, {-1, -1}},
	//	{{ 2, 0}, { 1,  0}},
	//	{{ 1, 0}, {-1,  0}},
	//	{{ 2, 2}, { 1,  1}},
	//	{{ 1, 2}, {-1,  1}}
	//	/// This is strictly for the linear streaming policy - Z is never negative
	//};

	///  Newbie implementation (above)

	//struct ModEntry {
	//	ModCoord key;
	//	aveng::ChunkCoord value;
	//};

	//constexpr std::array<ModEntry, 15> lmod{ {
	//	{{-2, 0}, {-1,  0}},
	//	{{-2, 1}, {-1, -1}},
	//	{{-2, 2}, {-1,  1}},
	//	{{-1, 0}, { 1,  0}},
	//	{{-1, 1}, { 1, -1}},
	//	{{-1, 2}, { 1,  1}},
	//	{{ 0, 0}, { 0,  0}},
	//	{{ 0, 1}, { 0, -1}},
	//	{{ 0, 2}, { 0,  1}},
	//	{{ 1, 0}, {-1,  0}},
	//	{{ 1, 1}, {-1, -1}},
	//	{{ 1, 2}, {-1,  1}},
	//	{{ 2, 0}, { 1,  0}},
	//	{{ 2, 1}, { 1, -1}},
	//	{{ 2, 2}, { 1,  1}},
	//} };

	//// my naive approach
	//inline aveng::ChunkCoord centerOfRegion_linearPolicy(int px, int pz, int offset= aveng::chunk_center_spacing) {
	//	
	//	ModCoord pmod = { px % offset, pz % offset };

	//	// Faster than a hash-map for just 9 elements
	//	for (const auto& e : lmod) {
	//		if (e.key.x == pmod.x && e.key.z == pmod.z) {
	//			return e.value;
	//		}
	//	}

	//	// Noob
	//	// if (auto coords = lmod.find(pmod); coords != lmod.end()) {
	//	// 	return coords->second;
	//	// }

	//	return { 999, 999 };
	//	
	//}
	//
	// Better use of this algo's symmetry - This works because Z is never negative 
	// and we're just tracking x as it wraps around the central point
	inline int wrapMod3(int v, int o) noexcept {
		return ((v % o) + o) % o; // "equivalence-class normalization"
	}

	// 3x3 table formula to know where we are in relation to the center coordinate
	inline aveng::ChunkCoord centerOfRegion_linearPolicy_wrap(int px, int pz, int offset = aveng::chunk_center_spacing) noexcept {
		const int mx = wrapMod3(px, offset);
		const int mz = wrapMod3(pz, offset);

		// mz == 0 means center row, mx == 0 means center column
		// mz == 1 means one row above center, mx == 1 means right column
		// mz == 2 means one row below center, mx == 2 means left column
		const int dx = (mx == 0) ? 0 : (mx == 1 ? -1 : 1);
		const int dz = (mz == 0) ? 0 : (mz == 1 ? -1 : 1);

		return { dx, dz };
	}

}