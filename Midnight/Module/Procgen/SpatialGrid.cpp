#include "SpatialGrid.h"

namespace aveng {

    // Access: [cell][i]
    std::span<const uint32_t> SpatialGrid::trianglesForCell(uint32_t cell) const {
        uint32_t begin = cellOffsets[cell];
        uint32_t end   = cellOffsets[cell + 1];
        return { cellTriangles.data() + begin, end - begin };
    }

    std::pair<int, bool> SpatialGrid::LocateTriangle(float x, float z) const {
        if (!delaunayMesh) return { -1, false };

        // Find the cell (Go's int() truncates toward zero; for non-negative coords this matches floor)
        const int cx = static_cast<int>((x - minx) / cellSize);
        const int cz = static_cast<int>((z - minz) / cellSize);

        if (cx < 0 || cx >= gridw || cz < 0 || cz >= gridh) {
            return { -1, false };
        }

        const uint32_t cellIdx = static_cast<uint32_t>(cz * gridw + cx);
        const Vec2 p{ x, z };

        // Candidate tris for this cell
        for (uint32_t ti : trianglesForCell(cellIdx)) {
            if (pointInTriangle(ti, p)) {
                return { static_cast<int>(ti), true };
            }
        }

        return { -1, false };
    }

    bool SpatialGrid::pointInTriangle(uint32_t ti, const Vec2& p) const {
        float wa, wb, wc;
        if (!delaunayMesh->Barycentric(ti, p, wa, wb, wc)) {
            return false;
        }

        // Point is inside if all weights are non-negative (with small epsilon for edge cases)
        constexpr float eps = -1e-6f; // you used -1e-9 in float64; loosen slightly for float
        return wa >= eps && wb >= eps && wc >= eps;
    }

    std::pair<float, bool> SpatialGrid::SampleHeight(float x, float z) const {
        const auto [tiInt, ok] = LocateTriangle(x, z);
        if (!ok) return { 0.f, false };

        const uint32_t ti = static_cast<uint32_t>(tiInt);
        const Vec2 p{ x, z };

        float wa, wb, wc;
        if (!delaunayMesh->Barycentric(ti, p, wa, wb, wc)) {
            return { 0.f, false };
        }

        const Triangle& t = delaunayMesh->tris[ti];

        // Discrete samples at triangle vertices
        // (Using span gives you .size() + bounds-friendly semantics in debug)
        if (static_cast<size_t>(t.A) >= heights.size() ||
            static_cast<size_t>(t.B) >= heights.size() ||
            static_cast<size_t>(t.C) >= heights.size()) {
            return { 0.f, false };
        }

        const float ha = heights[static_cast<size_t>(t.A)];
        const float hb = heights[static_cast<size_t>(t.B)];
        const float hc = heights[static_cast<size_t>(t.C)];

        return { wa * ha + wb * hb + wc * hc, true };
    }

    std::pair<Vec3, bool> SpatialGrid::GetTriangleNormal(float x, float z, std::span<const Vec3> normals) const {
        const auto [tiInt, ok] = LocateTriangle(x, z);
        if (!ok) return { Vec3{}, false };

        const uint32_t ti = static_cast<uint32_t>(tiInt);
        if (ti >= normals.size()) {
            return { Vec3{}, false };
        }

        return { normals[ti], true };
    }

    std::vector<uint32_t> SpatialGrid::TrianglesInBounds(
        const SpatialGrid& sg,
        float minX, float minZ, float maxX, float maxZ,
        TrianglesInBoundsScratch& scratch
    ) {
        if (!delaunayMesh) return {};

        scratch.ensure(delaunayMesh->tris.size());

        int cellMinX = static_cast<int>((minX - minx) / cellSize);
        int cellMaxX = static_cast<int>((maxX - minx) / cellSize);
        int cellMinZ = static_cast<int>((minZ - minz) / cellSize);
        int cellMaxZ = static_cast<int>((maxZ - minz) / cellSize);

        cellMinX = std::max(0, cellMinX);
        cellMinZ = std::max(0, cellMinZ);
        cellMaxX = std::min(sg.gridw - 1, cellMaxX);
        cellMaxZ = std::min(sg.gridh - 1, cellMaxZ);

        std::vector<uint32_t> result;
        result.reserve(64); // perf hint

        for (int cz = cellMinZ; cz <= cellMaxZ; ++cz) {
            for (int cx = cellMinX; cx <= cellMaxX; ++cx) {
                const uint32_t cellIdx = static_cast<uint32_t>(cz * gridw + cx);
                for (uint32_t ti : trianglesForCell(cellIdx)) {
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