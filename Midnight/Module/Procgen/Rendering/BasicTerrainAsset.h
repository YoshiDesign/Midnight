#pragma once
#include <vector>
#include "CoreVK/VkRenderData.h"

/**
 * Notes: https://chatgpt.com/g/g-p-67fd00e1b6748191913d770a83c124e5-vulkanengine/c/69a23f92-6b58-8330-824f-6c0b36959eec
 *  - vertices include halo (for correct normals across boundaries) TODO - Add a thin halo boundary to eliminate popping vertex normals
 *  - indices only include core-region triangles (to avoid double-drawing overlap)
 *  - CCW winding in world space (Test: If you have triangle vertex positions p0,p1,p2, compute cross(p1-p0, p2-p0) and compare to upDir. If it points down, swap 1 and 2)
 * 
 * Asset Construction (happens in ChunkManager):
 *	- iterate triangles in triangulation
 *	- compute triangle centroid (or all verts) and decide whether to keep triangle (core)
 *		- About this: keep the triangle based on its *circumcenter* being < max and > min vertex position.
 *		- We don't use centroids yet, but for this calculation it might actually be the better option.
 *	- add triangle indices (still indexing the full vertex array)
 *	- compute bounds using only core verts (or all, your call)
 * 
 * Pipeline:
 * Push TerrainDrawItem into FramePacketBuilder under the TerrainPipelineGroup:
 *	 - ChunkManager produces/updates ChunkRecords and triggers uploads
 *	 - TerrainSystem (runtime) queries records for visibility + readiness and builds draw list
 *	 - FramePacketBuilder consumes draw list
 * Recall that ChunkManager owns Terrain Chunks with a striped lock, so we don't want to query it directly for assets
 * 
 * [IMPORTANT] - Chunk Compute does not occur every frame, it's only needed once or if the chunk's data needs an update:
 *  - Lod Change, udpated heights, new upload, we change the compute's settings UBO, etc.
 * If the animation compute runs every frame, but terrain compute only runs when a chunk changes, 
 * you can schedule terrain dispatch conditionally.
 * This means we simply check for new chunks during Renderer::draw(), at the same point where we dispatch animation compute.
 * 
 * 
 * 
 */

/**
 * Terminology:
 * Core region = drawable 3x3
 * Support region = 5x5 packed compute input
 * Packed region = actual data ranges written into SSBOs
 * Output region = subset of vertices/triangles whose outputs are meaningful to graphics
 */

namespace procgen {

	// In case any configurables come to mind.
	struct TerrainBuilderOptions {
	
	};
	

	// TODO - Move descriptor sets here

	/*
		REFERENCE
	for (const auto& mesh : mModelMeshes) {
		VkVertexBufferData vertexBuffer;
		VertexBuffer::init(engineDevice, vertexBuffer, mesh.vertices.size() * sizeof(VkVertex));
		VertexBuffer::uploadData(engineDevice, vertexBuffer, mesh);
		mVertexBuffers.emplace_back(vertexBuffer);

		VkIndexBufferData indexBuffer;
		IndexBuffer::init(engineDevice, indexBuffer, mesh.indices.size() * sizeof(uint32_t));
		IndexBuffer::uploadData(engineDevice, indexBuffer, mesh);
		mIndexBuffers.emplace_back(indexBuffer);
	}
	
	*/

}