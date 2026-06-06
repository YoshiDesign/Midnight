#pragma once
#include <array>
#include <atomic>
#include "CoreVK/VkRenderData.h"
#include "Module/Procgen/Types2.h"
#include "Module/Procgen/Terrain/ChunkRecord2.h"
#include "Module/Procgen/TerrainArena.h"
#include "Utils/glm_includes.h"

/**
 * Other (possibly deprecated) Notes: https://chatgpt.com/g/g-p-67fd00e1b6748191913d770a83c124e5-vulkanengine/c/69a23f92-6b58-8330-824f-6c0b36959eec
 */

namespace procgen {

	constexpr uint32_t MAX_CHUNK_RECORDS = 49; // or 64, 128, etc.
	constexpr uint32_t MAX_TERRAIN_REQUESTS = 4; // maybe 2, 4, 8, etc.

	enum class TerrainRuntimeState : uint8_t
	{
		Unrequested,
		Requested,
		CpuReady,
		Uploading,
		Resident,
		Failed
	};

	struct ChunkHandle {
		uint32_t index{INVALID_CHUNK_INDEX}; // const from ChunkRecord2
		bool active{false};
		uint32_t generation{0}; // Unused until we need to promote a different architecture
		uint64_t frameRequested{ 0 }; 
	};

	/**
	 * CPU-side renderable for a 3x3 core region backed by a 5x5 support region.
	 *
	 * Layout convention for packed buffers: [3x3 core data | halo/support data]
	 * The compute shader reads the full buffer; the graphics pipeline only draws core.
	 */
	struct TerrainRenderable {

		uint32_t pos_offset;
		uint32_t tri_offset;
		uint32_t adj_offset;
		uint32_t vbo_offset;
		uint32_t ibo_offset;

		// Graphics pipeline inputs (3x3 core only)
		glm::vec3* vbo;       // Core vertex positions
		uint32_t*  ibo;       // Core triangle indices (into vbo)

		// Compute pipeline inputs -- flat [core | halo] layout
		glm::vec4*        packedPositions;  // All positions: core then halo (w = 1.0)
		glm::vec3*        packedTriangles;  // Site-index triples (bit-pattern uint32 in float, read as uvec3 on GPU)
		VertexAdjacency*  packedAdjacency;  // Per-vertex incident triangle list

		// Alignment metadata UBO (still useful for SSBO packing and descriptor offset computation)
		aveng::BasicTerrainAlignmentData alignment{};

		ChunkCoord center{-1, -1};

		//void resetKeepCapacity() {
		//	vbo.clear();
		//	ibo.clear();
		//	packedPositions.clear();
		//	packedTriangles.clear();
		//	packedAdjacency.clear();
		//	center = {};
		//	alignment = {};
		//}
	};

	struct CompletionNotice
	{
		uint32_t slotIndex;
		uint64_t requestId;
		bool success;
	};

	// In case any configurables come to mind.
	//struct TerrainBuilderOptions {
	//
	//};

	/* VK Resources */
	struct TerrainPackedGpuData
	{
		// Single VkBuffer for all compute inputs: [positions | triangles | adjacency]
		aveng::VkShaderStorageBufferData inputSsbo{};
		VkDeviceSize positionsOffset  = 0;  // byte offset into inputSsbo (always 0)
		VkDeviceSize trianglesOffset  = 0;  // byte offset, aligned to minStorageBufferOffsetAlignment
		VkDeviceSize adjacencyOffset  = 0;  // byte offset, aligned

		// Single VkBuffer for all compute outputs: [normals | steepness | weights]
		aveng::VkShaderStorageBufferData outputSsbo{};
		VkDeviceSize normalsOffset    = 0;  // byte offset (always 0)
		VkDeviceSize steepnessOffset  = 0;  // byte offset, aligned
		VkDeviceSize weightsOffset    = 0;  // byte offset, aligned

		// Per-chunk descriptor set for compute bindings
		VkDescriptorSet computeDescriptorSet = VK_NULL_HANDLE;

		// Per-chunk descriptor set for lit graphics bindings (normals, weights, steepness SSBOs)
		VkDescriptorSet graphicsDescriptorSet = VK_NULL_HANDLE;

		// Counts for dispatch sizing / push constants
		uint32_t totalVerts  = 0;  // core + halo
		uint32_t coreVerts   = 0;  // core only (dispatch size = numVertices)
		uint32_t totalTris   = 0;  // core + halo (push constant numTriangles)
	};

	struct TerrainDrawGpuData
	{
		aveng::VkVertexBufferData vertexBuffer{};
		aveng::VkIndexBufferData  indexBuffer{};
		uint32_t indexCount = 0;
		uint32_t vertexCount = 0;
	};

	struct TerrainGpuChunk
	{
		TerrainDrawGpuData draw;
		TerrainPackedGpuData packed;
		bool valid = false;
	};
	/* */

	// New Primary resource pool for all terrain happenings. 
	// Owns all of its resources.
	// Perhaps humorous is my std::array of pointers to arena allocations
	struct TerrainPool {

		// State
		uint32_t nActive{ 0 };
		size_t capacity{ MAX_CHUNK_RECORDS };

		// Synchronization & Validation
		std::array<std::atomic<bool>, MAX_CHUNK_RECORDS> in_use_flag; 
		std::array<std::atomic<RenderableBuildState>, MAX_CHUNK_RECORDS> build_state_flag;

		std::array<int, MAX_CHUNK_RECORDS> current_request_id; // For validating async renderable requests

		// Primary chunk data - These hold pointers into _scratch and _final
		std::array<ChunkRecord2, MAX_CHUNK_RECORDS> records{};

		std::array<TerrainRenderable, MAX_CHUNK_RECORDS> renderable{};

		// Arenas
		std::array<ScratchArena, MAX_CHUNK_RECORDS> _scratch{};
		std::array<ScratchArena, MAX_CHUNK_RECORDS> _final{};
	};

}