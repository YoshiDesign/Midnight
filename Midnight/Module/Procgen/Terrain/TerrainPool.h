#pragma once
#include <cstdint>
#include "Module/Procgen/Types2.h"
#include "Module/Procgen/Terrain/ChunkRecord2.h"
#include "Module/Procgen/Rendering/BasicTerrainAsset.h"

namespace procgen {

	/*
	* Terrain Pool notes:
	* ChunkRecord* rec = &pool->slots_[i].record; // Try not to do this - always prefer a handle
	*/
	
	//struct ChunkSlot
	//{
	//	ChunkRecord2 record{};
	//	uint32_t generation{0};
	//	bool active{false};

	//	TerrainRuntimeState state{ TerrainRuntimeState::Unrequested };

	//	TerrainRenderable renderable;
	//	TerrainGpuChunk gpu;
	//};
	//
	//struct ChunkHandle {
	//	uint32_t index;
	//	bool active;
	//	uint32_t generation;
	//};
	//

	inline void InitTerrainPool(TerrainPool* pool) {
		
	}
	
	//inline ChunkHandle AddTerrain(TerrainPool* pool) {
	//	pool->slots_[pool->nActive].generation = 1;
	//	pool->slots_[pool->nActive].active = true;
	//	return { pool->nActive++, true };

	//}
	//
	//inline void RemoveTerrain(TerrainPool* pool, ChunkHandle handle) {
	//	if (pool == nullptr) return;

	//	uint32_t last_index = pool->nActive - 1;

	//	// swap
	//	pool->slots_[handle.index].record = pool->slots_[last_index].record;

	//	// pop
	//	pool->nActive--;

	//	pool->slots_[handle.index].generation++;
	//	handle.generation++;

	//	return;
	//}
}