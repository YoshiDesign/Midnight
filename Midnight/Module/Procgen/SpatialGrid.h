#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <memory>
#include <utility>
#include <algorithm>
#include "Core/Math/Vector.h" // Vec2, Vec3, clampInt, etc.
#include "Module/Procgen/Types.h"

namespace aveng {

    // Forward decls (adjust include strategy to match your codebase)
    struct Triangle;
    struct Triangulation;
    struct AllPoints;
    struct HeightField;

    struct SpatialGrid
    {
        // ---- Backing data (non-owning) these point directly to ChunkRecord's products ----
        // All pointers are assumed valid for the SpatialGrid lifetime (per our design).
        const Triangulation* tri = nullptr;   // provides tri->tris
        const AllPoints* pts = nullptr;   // provides pts->pts (Vec2 positions)
        const HeightField* hf = nullptr;   // provides hf->heights

        // Convenience spans (set at build time; derived from pointers above)
        std::span<const Triangle> tris;
        std::span<const Vec2>     vertexPos;
        std::span<const float>    heights;

        size_t vertexCount = 0;

        // ---- Grid config ----
        float minx = 0.f, minz = 0.f, maxx = 0.f, maxz = 0.f;
        int   gridw = 0, gridh = 0;
        float cellSize = 0.f;

        // ---- Cell -> triangles mapping ----
        // cellOffsets size = numCells + 1
        // cellTriangles is flat triangle-index list
        std::vector<uint32_t> cellOffsets;
        std::vector<TriIndex> cellTriangles;

        // ---- Local/heap scratch for "unique triangle set" queries ----
        struct TrianglesInBoundsScratch
        {
            std::vector<TriIndex> stamp;
            uint32_t epoch = 1;

            void ensure(size_t n)
            {
                if (stamp.size() != n) {
                    stamp.assign(n, 0);
                    epoch = 1;
                }
                if (++epoch == 0) {
                    std::fill(stamp.begin(), stamp.end(), 0);
                    epoch = 1;
                }
            }
        };

        // ---- Queries ----

        // Alias
        std::pair<float, bool> Raycast(float x, float z) const { return SampleHeight(x, z); }

        std::span<const TriIndex> trianglesForCell(uint32_t cell) const
        {
            const uint32_t begin = cellOffsets[cell];
            const uint32_t end = cellOffsets[cell + 1];
            return { cellTriangles.data() + begin, end - begin };
        }

        std::pair<TriIndex, bool> LocateTriangle(float x, float z) const;

        bool pointInTriangle(TriIndex ti, const Vec2& p) const;

        // Returns (height, ok) ok = non-degenerate triangle
        std::pair<float, bool> SampleHeight(float x, float z) const;

        // normals is expected to be per-triangle (same length as tris)
        std::pair<Vec3, bool> GetTriangleNormal(float x, float z, std::span<const Vec3> normals) const;

        // Deduped list of triangle indices overlapping the given AABB in x/z.
        std::vector<TriIndex> TrianglesInBounds(
            float qMinX, float qMinZ, float qMaxX, float qMaxZ,
            TrianglesInBoundsScratch& scratch
        ) const;

        bool valid() const
        {
            return tri && pts && hf &&
                !tris.empty() &&
                vertexPos.size() >= 3 &&
                heights.size() == vertexPos.size() &&
                gridw > 0 && gridh > 0 &&
                cellSize > 0.f &&
                cellOffsets.size() == static_cast<size_t>(gridw * gridh + 1);
        }
    };

    // Build function: uses ChunkRecord-owned stage data directly.
    std::unique_ptr<SpatialGrid> BuildSpatialGrid(
        const Triangulation* triangulation,
        const AllPoints* allPoints,
        const HeightField* heightField,
        float cellSize,
        float minX, float minZ,
        float maxX, float maxZ
    );

}