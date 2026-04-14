#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <memory>
#include <utility>
#include <algorithm>
#include "Core/Math/Vector.h" // Vec2, Vec3, clampInt, etc.
#include "Module/Procgen/Types2.h"

namespace procgen {

    struct Triangulation;
    struct AllPoints;
    struct HeightField;

    struct SpatialGrid2
    {
        // ---- Backing data (non-owning) these point directly to ChunkRecord's products ----
        // All pointers are assumed valid for the SpatialGrid lifetime (per our design).
        const Triangulation* tri = nullptr;   // provides tri->tris
        const AllPoints* pts = nullptr;   // provides pts->pts (Vec2 positions)
        const HeightField* hf = nullptr;   // provides hf->heights

        // Convenience spans (set at build time; derived from pointers above)
        std::span<const Triangle> tris;
        std::span<const aveng::Vec2>     vertexPos;
        std::span<const float>    heights;

        size_t vertexCount = 0;

        // ---- Grid config ----
        float minx = 0.f, minz = 0.f, maxx = 0.f, maxz = 0.f;
        int   gridw = 0, gridh = 0;
        float cellSize = 0.f;

        Bounds2 worldBounds{}; // min/max dimensions. Core + Halo

        // ---- Cell -> triangles mapping ----
        // cellOffsets size = numCells + 1
        // cellTriangles is flat triangle-index list
        std::vector<uint32_t> cellOffsets;
        std::vector<TriIndex> cellTriangles;

    };

    // Build function: uses ChunkRecord-owned stage data directly.
    SpatialGrid2* BuildSpatialGrid(
        const Triangulation* triangulation,
        const AllPoints* allPoints,
        const HeightField* heightField,
        float cellSize,
        float minX, float minZ,
        float maxX, float maxZ
    );

    inline std::span<const TriIndex> trianglesForCell(SpatialGrid2* sg, uint32_t cell)
    {
        const uint32_t begin = sg->cellOffsets[cell];
        const uint32_t end = sg->cellOffsets[cell + 1];
        return { sg->cellTriangles.data() + begin, end - begin };
    }

    TriIndex LocateTriangle(SpatialGrid2* sg, float x, float z);
    bool pointInTriangle(SpatialGrid2* sg, TriIndex ti, const aveng::Vec2& p);
}