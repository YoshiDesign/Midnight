#include "SpatialGrid.h"
#include "Core/Math/Math.h"
#include "Module/Procgen/Delaunay.h"
#include "Module/Procgen/Terrain/ChunkRecord.h"

namespace aveng {
    // -----------------------------
      // BuildSpatialGrid
      // -----------------------------
    std::unique_ptr<SpatialGrid> BuildSpatialGrid(
        const Triangulation* triangulation,
        const AllPoints* allPoints,
        const HeightField* heightField,
        float cellSize,
        float minX, float minZ,
        float maxX, float maxZ
    ) {
        if (!triangulation || !allPoints || !heightField) {
            return nullptr;
        }
        if (triangulation->tris.empty()) {
            return nullptr;
        }

        const auto& triVec = triangulation->tris;
        const auto& posVec = allPoints->pts;
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
            const Vec2 a = sg->vertexPos[t.A]; // [IMPORTANT] SiteIndex is a 32-bit type. Keep in mind if we decide to go crazy on resolution.
            const Vec2 b = sg->vertexPos[t.B]; // [IMPORTANT] SiteIndex is a 32-bit type. Keep in mind if we decide to go crazy on resolution.
            const Vec2 c = sg->vertexPos[t.C]; // [IMPORTANT] SiteIndex is a 32-bit type. Keep in mind if we decide to go crazy on resolution.

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
            cellMinX = clampInt(cellMinX, 0, gridW - 1);
            cellMaxX = clampInt(cellMaxX, 0, gridW - 1);
            cellMinZ = clampInt(cellMinZ, 0, gridH - 1);
            cellMaxZ = clampInt(cellMaxZ, 0, gridH - 1);

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

            const Vec2 a = sg->vertexPos[t.A];
            const Vec2 b = sg->vertexPos[t.B];
            const Vec2 c = sg->vertexPos[t.C];

            const float triMinX = std::min({ a.x, b.x, c.x });
            const float triMaxX = std::max({ a.x, b.x, c.x });
            const float triMinZ = std::min({ a.y, b.y, c.y });
            const float triMaxZ = std::max({ a.y, b.y, c.y });

            int cellMinX = (triMinX - minX) / cellSize;
            int cellMaxX = (triMaxX - minX) / cellSize;
            int cellMinZ = (triMinZ - minZ) / cellSize;
            int cellMaxZ = (triMaxZ - minZ) / cellSize;

            cellMinX = clampInt(cellMinX, 0, gridW - 1);
            cellMaxX = clampInt(cellMaxX, 0, gridW - 1);
            cellMinZ = clampInt(cellMinZ, 0, gridH - 1);
            cellMaxZ = clampInt(cellMaxZ, 0, gridH - 1);

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

    bool SpatialGrid::valid() const {
        return tri && pts && hf &&
            !tris.empty() &&
            vertexPos.size() >= 3 &&
            heights.size() == vertexPos.size() &&
            gridw > 0 && gridh > 0 &&
            cellSize > 0.f &&
            cellOffsets.size() == static_cast<size_t>(gridw * gridh + 1);
    }

    // -----------------------------
    // SpatialGrid methods
    // -----------------------------

    std::pair<TriIndex, bool> SpatialGrid::LocateTriangle(float x, float z) const
    {
        if (!tri || !pts || tris.empty()) {
            return { -1, false };
        }

        const int cx = (x - minx) / cellSize; // Implicit truncation toward 0
        const int cz = (z - minz) / cellSize; // Implicit truncation toward 0

        if (cx < 0 || cx >= gridw || cz < 0 || cz >= gridh) {
            return { -1, false };
        }

        const uint32_t cellIdx = cz * gridw + cx; // God help you if this exceeds 2,147,483,647
        const Vec2 p{ x, z };

        for (TriIndex ti : trianglesForCell(cellIdx)) {
            if (pointInTriangle(ti, p)) {
                return { static_cast<int>(ti), true };
            }
        }

        return { -1, false };
    }

    bool SpatialGrid::pointInTriangle(TriIndex ti, const Vec2& p) const
    {
        if (!pts || !tri) return false;

        BaryWeights w{};
        constexpr float denomEps = 1e-8f; // for degenerate tri rejection

        if (!Barycentric(*pts, *tri, ti, p, w, denomEps)) {
            return false;
        }

        // Inside test epsilon (edge tolerance)
        constexpr float insideEps = -1e-6f;
        return (w.wa >= insideEps) && (w.wb >= insideEps) && (w.wc >= insideEps);
    }

    std::pair<float, bool> SpatialGrid::SampleHeight(float x, float z) const
    {
        const auto [ti, ok] = LocateTriangle(x, z);
        if (!ok) return { 0.f, false }; // Some Go flavor

        if (!pts || !tri) return { 0.f, false };

        const Vec2 p{ x, z };

        BaryWeights w{};
        constexpr float denomEps = 1e-8f;

        if (!Barycentric(*pts, *tri, ti, p, w, denomEps)) {
            return { 0.f, false };
        }

        const Triangle& t = tri->tris[ti];

        if (t.A >= heights.size() || t.B >= heights.size() || t.C >= heights.size()) {
            return { 0.f, false };
        }

        const float ha = heights[t.A];
        const float hb = heights[t.B];
        const float hc = heights[t.C];

        return { w.wa * ha + w.wb * hb + w.wc * hc, true };
    }

    std::pair<Vec3, bool> SpatialGrid::GetTriangleNormal(float x, float z, std::span<const Vec3> normals) const
    {
        const auto [ti, ok] = LocateTriangle(x, z);
        if (!ok) return { Vec3{}, false };

        if (ti >= normals.size()) {
            return { Vec3{}, false };
        }
        return { normals[ti], true };
    }

    std::vector<TriIndex> SpatialGrid::TrianglesInBounds(
        float qMinX, float qMinZ, float qMaxX, float qMaxZ,
        TrianglesInBoundsScratch& scratch
    ) const {
        if (!tri || tris.empty()) return {};

        scratch.ensure(tris.size());

        int cellMinX = (qMinX - minx) / cellSize; // More implicit conversion for -Wall heart attacks
        int cellMaxX = (qMaxX - minx) / cellSize;
        int cellMinZ = (qMinZ - minz) / cellSize;
        int cellMaxZ = (qMaxZ - minz) / cellSize;

        cellMinX = std::max(0, cellMinX);
        cellMinZ = std::max(0, cellMinZ);
        cellMaxX = std::min(gridw - 1, cellMaxX);
        cellMaxZ = std::min(gridh - 1, cellMaxZ);

        std::vector<TriIndex> result;
        result.reserve(64);

        for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
            for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
                const uint32_t cellIdx = cz * gridw + cx; 
                for (TriIndex ti : trianglesForCell(cellIdx)) {
                    if (scratch.stamp[ti] != scratch.epoch) {
                        scratch.stamp[ti] = scratch.epoch;
                        result.push_back(ti);
                    }
                }
            }
        }

        return result;
    }

}