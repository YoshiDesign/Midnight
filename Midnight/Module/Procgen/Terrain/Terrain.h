#pragma once

#include <memory>
#include <span>

#include "Module/Procgen/SpatialGrid.h"
#include "Core/Math/Math.h"


namespace aveng {

	inline std::unique_ptr<SpatialGrid> BuildSpatialGrid(
		const DelaunayMeshView* mesh, // We need to pass in the PMR data, not the DelaunayMeshView now that it's just a blob of helpers
		const std::vector<float>& heights,
		float cellSize,
		float minX, float minZ,
		float maxX, float maxZ
	) {
		if (!mesh || mesh->tris.empty()) {
			return nullptr;
		}
	
		const float width  = maxX - minX;
		const float height = maxZ - minZ;
		if (width <= 0.0 || height <= 0.0 || cellSize <= 0.0) {
			return nullptr;
		}
	
		int gridW = static_cast<int>(std::ceil(width / cellSize));
		int gridH = static_cast<int>(std::ceil(height / cellSize));
		if (gridW <= 0) gridW = 1;
		if (gridH <= 0) gridH = 1;
	
		const int numCells = gridW * gridH;
	
		auto sg = std::make_unique<SpatialGrid>();
		sg->delaunayMesh = mesh; // Used to call methods of DelaunayMeshView
		sg->heights = std::span<const float>(heights);
		sg->vertexCount = heights.size();
		sg->cellSize = cellSize;
		sg->minx = minX;
		sg->minz = minZ;
		sg->maxx = maxX;
		sg->maxz = maxZ;
		sg->gridw = gridW;
		sg->gridh = gridH;
	
		// Pass 1: count how many triangle references each cell will have
		std::vector<uint32_t> counts(static_cast<size_t>(numCells), 0);
	
		for (uint32_t ti = 0; ti < static_cast<uint32_t>(mesh->tris.size()); ++ti) {
			const Triangle& t = mesh->tris[ti];
	
			const Vec2 a = mesh->vertexPos[t.A];
			const Vec2 b = mesh->vertexPos[t.B];
			const Vec2 c = mesh->vertexPos[t.C];
	
			// The AABB for this triangle
			const float triMinX = std::min({a.x, b.x, c.x});
			const float triMaxX = std::max({a.x, b.x, c.x});
			const float triMinZ = std::min({a.y, b.y, c.y});
			const float triMaxZ = std::max({a.y, b.y, c.y});
	
			int cellMinX = static_cast<int>((triMinX - minX) / cellSize);
			int cellMaxX = static_cast<int>((triMaxX - minX) / cellSize);
			int cellMinZ = static_cast<int>((triMinZ - minZ) / cellSize);
			int cellMaxZ = static_cast<int>((triMaxZ - minZ) / cellSize);
	
			cellMinX = clampInt(cellMinX, 0, gridW - 1);
			cellMaxX = clampInt(cellMaxX, 0, gridW - 1);
			cellMinZ = clampInt(cellMinZ, 0, gridH - 1);
			cellMaxZ = clampInt(cellMaxZ, 0, gridH - 1);
	
			for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
				for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
					const int cellIdx = cz * gridW + cx;
					counts[static_cast<size_t>(cellIdx)]++;
				}
			}
		}
	
		// Prefix sum to compute offsets
		sg->cellOffsets.resize(static_cast<size_t>(numCells) + 1, 0);
		for (int i = 0; i < numCells; ++i) {
			sg->cellOffsets[static_cast<size_t>(i) + 1] =
				sg->cellOffsets[static_cast<size_t>(i)] + counts[static_cast<size_t>(i)];
		}
	
		const uint32_t totalRefs = sg->cellOffsets.back();
		sg->cellTriangles.resize(static_cast<size_t>(totalRefs));
	
		// Pass 2: fill. We need per-cell write cursors.
		// We'll reuse counts[] as a "cursor" array by resetting to 0.
		std::fill(counts.begin(), counts.end(), 0);
	
		for (uint32_t ti = 0; ti < static_cast<uint32_t>(mesh->tris.size()); ++ti) {
			const Triangle& t = mesh->tris[ti];
	
			const Vec2 a = mesh->vertexPos[t.A];
			const Vec2 b = mesh->vertexPos[t.B];
			const Vec2 c = mesh->vertexPos[t.C];
	
			// vars named in our handedness (RH, +z is forward)
			const float triMinX = std::min({a.x, b.x, c.x});
			const float triMaxX = std::max({a.x, b.x, c.x});
			const float triMinZ = std::min({a.y, b.y, c.y});
			const float triMaxZ = std::max({a.y, b.y, c.y});
	
			int cellMinX = static_cast<int>((triMinX - minX) / cellSize);
			int cellMaxX = static_cast<int>((triMaxX - minX) / cellSize);
			int cellMinZ = static_cast<int>((triMinZ - minZ) / cellSize);
			int cellMaxZ = static_cast<int>((triMaxZ - minZ) / cellSize);
	
			cellMinX = clampInt(cellMinX, 0, gridW - 1);
			cellMaxX = clampInt(cellMaxX, 0, gridW - 1);
			cellMinZ = clampInt(cellMinZ, 0, gridH - 1);
			cellMaxZ = clampInt(cellMaxZ, 0, gridH - 1);
	
			for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
				for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
					const int cellIdx = cz * gridW + cx;
					const size_t ci = static_cast<size_t>(cellIdx);
	
					const uint32_t writeBase = sg->cellOffsets[ci];
					const uint32_t writeAt   = writeBase + counts[ci]; // cursor
					sg->cellTriangles[static_cast<size_t>(writeAt)] = ti;
					counts[ci]++; // advance cursor
				}
			}
		}
	
		return sg;
	}

}