#pragma once

#include <vector>
#include <memory_resource>

#include "Core/Math/Vector.h"
#include "Module/Procgen/Types.h"

/**
 * Performance Improvements:
 * If you want a more meaningful speedup later, consider one of these:
 * - Use sincos (if available): compute both at once (sincos / std::sincos isn't standard everywhere, but many platforms have it).
 * - Precompute a table of (cos, sin) for N angles (e.g. 2048) and sample an index from RNG:
 *   - huge reduction in trig cost
 *   - usually changes distribution negligibly for this use case
 * - Use a fast approximate sin/cos if you're okay with small error.
 */

namespace aveng {
	
	struct BlueNoiseConfig {
		float MinDist = 0.0;
		int MaxTries = 30; // default matches your Go behavior
	};
	
	// Forward declare your RNG type + seeding + helpers.
	// (Include your Rng.h here if you prefer.)
	struct Rng;
	void SeedRng(Rng& rng, uint64_t seed);
	float Uniform01d(Rng& rng);
	int32_t UniformInt(Rng& rng, int32_t minVal, int32_t maxVal);
	
	// Core APIs (Go parity)
	std::pmr::vector<Vec2> GenerateBlueNoise(
		Rng& rng,
		float minX, float minZ,
		float maxX, float maxZ,
		BlueNoiseConfig cfg,
		std::pmr::memory_resource* mr
	);
	
	std::pmr::vector<Vec2> GenerateBlueNoiseSeeded(
		int64_t seed,
		float minX, float minZ,
		float maxX, float maxZ,
		BlueNoiseConfig cfg,
		std::pmr::memory_resource* mr
	);
	
}