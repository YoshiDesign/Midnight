#include "SpatialGrid2.h"
#include "Core/Math/Math.h"
#include "Module/Procgen/Delaunay2.h"
#include "Module/Procgen/Terrain/ChunkRecord2.h"


namespace procgen {

    SpatialGrid2* BuildSpatialGrid(const Triangulation* triangulation, const AllPoints* allPoints, const HeightField* heightField, float cellSize, float minX, float minZ, float maxX, float maxZ)
    {
        if (!triangulation || !allPoints || !heightField) {
            return nullptr;
        }
        if (triangulation->size_tris == 0) {
            return nullptr;
        }

        const auto& triVec = triangulation->tris;
        const auto& posVec = allPoints->all_pts;
        const auto& hVec = heightField->heights;

        // We assume triangulation indices refer into allPoints->pts and heightField->heights
        if (posVec.empty() || hVec.empty() || posVec.size() != hVec.size()) {
            return nullptr;
        }

        const float width = maxX - minX; // This should be the same for every chunk, just saying.
        const float height = maxZ - minZ; // same
        if (width <= 0.0f || height <= 0.0f || cellSize <= 0.0f) {
            return nullptr;
        }

        int gridW = std::ceil(width / cellSize);
        int gridH = std::ceil(height / cellSize);
        if (gridW <= 0) gridW = 1;
        if (gridH <= 0) gridH = 1;

        const int numCells = gridW * gridH;

        auto sg = std::make_unique<SpatialGrid>();

        // Wire pointers/spans to ChunkRecord-owned data
        sg->tri = triangulation;
        sg->pts = allPoints;
        sg->hf = heightField;

        // "Convenience Spans"
        sg->tris = std::span<const Triangle>(triVec.data(), triVec.size());
        sg->vertexPos = std::span<const Vec2>(posVec.data(), posVec.size());
        sg->heights = std::span<const float>(hVec.data(), hVec.size());
        sg->vertexCount = sg->heights.size();

        sg->cellSize = cellSize;
        sg->minx = minX;
        sg->minz = minZ;
        sg->maxx = maxX;
        sg->maxz = maxZ;

        // world space min and max for the chunk
        sg->worldBounds = { minX, minZ, maxX, maxZ };

        sg->gridw = gridW;
        sg->gridh = gridH;

        // Pass 1: count triangle references per cell
        // These values have a few uses here.
        std::vector<uint32_t> counts(numCells, 0);

        for (uint32_t ti = 0; ti < sg->tris.size(); ++ti) {
            const Triangle& t = sg->tris[ti];

            // Triangle vertices
            const aveng::Vec2 a = sg->vertexPos[t.A]; // [IMPORTANT] SiteIndex is a 32-bit type. Keep in mind if we decide to go crazy on resolution.
            const aveng::Vec2 b = sg->vertexPos[t.B]; // [IMPORTANT] SiteIndex is a 32-bit type. Keep in mind if we decide to go crazy on resolution.
            const aveng::Vec2 c = sg->vertexPos[t.C]; // [IMPORTANT] SiteIndex is a 32-bit type. Keep in mind if we decide to go crazy on resolution.

            // Triangle AABB
            const float triMinX = std::min({ a.x, b.x, c.x });
            const float triMaxX = std::max({ a.x, b.x, c.x });
            const float triMinZ = std::min({ a.y, b.y, c.y });
            const float triMaxZ = std::max({ a.y, b.y, c.y });

            // Cell AABB that this triangle lands in
            int cellMinX = /* static_cast<int> ( */(triMinX - minX) / cellSize /* )*/; // Warning - Implicit conversion
            int cellMaxX = /* static_cast<int> ( */(triMaxX - minX) / cellSize /* )*/; // Warning - Implicit conversion
            int cellMinZ = /* static_cast<int> ( */(triMinZ - minZ) / cellSize /* )*/; // Warning - Implicit conversion
            int cellMaxZ = /* static_cast<int> ( */(triMaxZ - minZ) / cellSize /* )*/; // Warning - Implicit conversion

            // clamp to chunk bounds
            cellMinX = aveng::clampInt(cellMinX, 0, gridW - 1);
            cellMaxX = aveng::clampInt(cellMaxX, 0, gridW - 1);
            cellMinZ = aveng::clampInt(cellMinZ, 0, gridH - 1);
            cellMaxZ = aveng::clampInt(cellMaxZ, 0, gridH - 1);

            // 
            for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
                for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
                    const int cellIdx = cz * gridW + cx;
                    // Increment the number of triangles in this cell
                    counts[cellIdx]++;
                }
            }
        }

        // Prefix sum -> offsets
        sg->cellOffsets.assign(numCells + 1, 0);
        for (size_t i = 0; i < numCells; ++i) {
            // Cell 0's offset is always 0. derp.
            // Standard base + stride indexing for cells and their triangles.
            // Note that we're doing this to be able to pre-size cellTriangles
            sg->cellOffsets[i + 1] = sg->cellOffsets[i] + counts[i];
        }

        // const size_t totalRefs = sg->cellOffsets.back();
        sg->cellTriangles.resize(sg->cellOffsets.back());

        // Pass 2: fill using per-cell cursors (reuse counts)
        std::fill(counts.begin(), counts.end(), 0);
        for (uint32_t ti = 0; ti < static_cast<uint32_t>(sg->tris.size()); ++ti) {

            // TODO: Cache the results from the first pass

            const Triangle& t = sg->tris[ti];

            const aveng::Vec2 a = sg->vertexPos[t.A];
            const aveng::Vec2 b = sg->vertexPos[t.B];
            const aveng::Vec2 c = sg->vertexPos[t.C];

            const float triMinX = std::min({ a.x, b.x, c.x });
            const float triMaxX = std::max({ a.x, b.x, c.x });
            const float triMinZ = std::min({ a.y, b.y, c.y });
            const float triMaxZ = std::max({ a.y, b.y, c.y });

            int cellMinX = (triMinX - minX) / cellSize;
            int cellMaxX = (triMaxX - minX) / cellSize;
            int cellMinZ = (triMinZ - minZ) / cellSize;
            int cellMaxZ = (triMaxZ - minZ) / cellSize;

            cellMinX = aveng::clampInt(cellMinX, 0, gridW - 1);
            cellMaxX = aveng::clampInt(cellMaxX, 0, gridW - 1);
            cellMinZ = aveng::clampInt(cellMinZ, 0, gridH - 1);
            cellMaxZ = aveng::clampInt(cellMaxZ, 0, gridH - 1);

            for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
                for (int cx = cellMinX; cx <= cellMaxX; ++cx) {

                    const size_t cellIdx = cz * gridW + cx;
                    const size_t writeBase = sg->cellOffsets[cellIdx];  // Acquire the base triangle index for this cell
                    const size_t writeAt = writeBase + counts[cellIdx]; // Next available write position for this cell

                    sg->cellTriangles[writeAt] = ti;
                    counts[cellIdx]++; // We increment to influence writeAt for when we land in this cell again
                }
            }
        }

        return sg;
    }

    /**
     * Locate a triangle within a chunk given world coordinates.
     * Remember that each chunk has its own SpatialGrid
     */
	TriIndex LocateTriangle(SpatialGrid2* sg, float x, float z)
	{

        if (!sg->tri || !sg->pts || sg->tris.empty()) {
            return kInvalidTri;
        }

        const int cx = (x - sg->minx) / sg->cellSize; // Implicit truncation toward 0
        const int cz = (z - sg->minz) / sg->cellSize; // Implicit truncation toward 0

        if (cx < 0 || cx >= sg->gridw || cz < 0 || cz >= sg->gridh) {
            return kInvalidTri;
        }

        const uint32_t cellIdx = cz * sg->gridw + cx; // God help you if this exceeds 2,147,483,647
        const aveng::Vec2 p{ x, z };

        for (TriIndex ti : trianglesForCell(sg, cellIdx)) {
            if (pointInTriangle(sg, ti, p)) {
                return static_cast<int>(ti);
            }
        }

        return kInvalidTri;
        
	}
     
	// Point-in-triangle test using barycentric coordinates. Allows points on edge.
    bool pointInTriangle(SpatialGrid2* sg, TriIndex ti, const aveng::Vec2& p)
    {
        if (!sg->pts || !sg->tri) return false;

        aveng::BaryWeights w{};
        constexpr float denomEps = 1e-8f; // for degenerate tri rejection

        if (!Barycentric(*sg->pts, *sg->tri, ti, p, w, denomEps)) {
            return false;
        }

        // Inside test epsilon (edge tolerance)
        constexpr float insideEps = -1e-6f;
        return (w.wa >= insideEps) && (w.wb >= insideEps) && (w.wc >= insideEps);
    }

}