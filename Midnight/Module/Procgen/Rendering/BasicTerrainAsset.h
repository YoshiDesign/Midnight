#pragma once
#include <vector>
#include <memory>
#include "CoreVK/VkRenderData.h"
#include "Module/Procgen/Types.h"
#include "Utils/glm_includes.h"

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

	/**
	 * CPU-side renderable for a 3x3 core region backed by a 5x5 support region.
	 *
	 * Layout convention for packed buffers: [3x3 core data | halo/support data]
	 * The compute shader reads the full buffer; the graphics pipeline only draws core.
	 */
	struct TerrainRenderable {

		// Graphics pipeline inputs (3x3 core only)
		std::vector<glm::vec3> vbo;       // Core vertex positions
		std::vector<uint32_t>  ibo;       // Core triangle indices (into vbo)

		// Compute pipeline inputs -- flat [3x3_core | halo] layout
		std::vector<glm::vec3>        packedPositions;  // All positions: core then halo
		std::vector<glm::vec3>        packedTriangles;  // Site-index triples (bit-pattern uint32 in float, read as uvec3 on GPU)
		std::vector<VertexAdjacency>  packedAdjacency;  // Per-vertex incident triangle list

		// Alignment metadata (maps 1:1 to BasicTerrainAlignmentData UBO, binding 9)
		aveng::BasicTerrainAlignmentData alignment{};

		aveng::ChunkCoord center{};
	};

	struct RenderableCompletion
	{
		aveng::ChunkCoord coord;
		uint64_t requestId; // Build generation id
		bool success;
	};

	struct RenderableCompletion_ALT
	{
		aveng::ChunkCoord coord;
		uint64_t requestId;
		bool success;
		std::unique_ptr<TerrainRenderable> renderable;
	};

	// In case any configurables come to mind.
	struct TerrainBuilderOptions {
	
	};

}