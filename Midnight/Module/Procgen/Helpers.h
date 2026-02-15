#pragma once
#include <stdint.h>
#include "Module/Procgen/Types.h"

namespace aveng {
	// Deterministic chunk seed (FNV-1a 64-bit), Go parity
	int64_t ChunkSeed(int64_t worldSeed, ChunkCoord coord);
}