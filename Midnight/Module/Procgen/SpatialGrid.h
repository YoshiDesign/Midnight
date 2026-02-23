#pragma once
#include <vector>
#include <span>
#include <unordered_set>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "Module/Procgen/Delaunay.h"
#include "Module/Procgen/Types.h"


namespace aveng {

    class SpatialGrid {
    public:

        const DelaunayMeshView* delaunayMesh; // Used as a "view" into the data that represents delaunay mesh.
                                              // Prefer this over using resources of the ChunkRecord directly.
        std::span<const float> heights;       // Provided by the ChunkRecord.

        // Config
        float minx = 0.f, minz = 0.f, maxx = 0.f, maxz = 0.f;
        int gridw = 0, gridh = 0;
        float cellSize = 0.f;
        size_t vertexCount = 0;

        // Thread safe and cheap
        struct TrianglesInBoundsScratch {
            std::vector<uint32_t> stamp;
            uint32_t epoch = 1;

            void ensure(size_t n) {
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

        SpatialGrid() = default;
        ~SpatialGrid();

        // Just an alias
        std::pair<float, bool> Raycast(float x, float z) const {
            return SampleHeight(x, z);
        }

        std::span<const uint32_t> trianglesForCell(uint32_t cell) const;

        // This is how we manage a 2D grid of triangles while remaining cache-friendly.
        std::vector<uint32_t> cellOffsets;   // size = numCells + 1
        std::vector<uint32_t> cellTriangles; // flat triangle indices

        std::pair<int, bool> LocateTriangle(float x, float z) const;

        bool pointInTriangle(uint32_t ti, const Vec2& p) const;

        std::pair<float, bool> SampleHeight(float x, float z) const;

        std::pair<Vec3, bool> GetTriangleNormal(float x, float z, std::span<const Vec3> normals) const;

        std::vector<uint32_t> TrianglesInBounds(
            const SpatialGrid& sg,
            float minX, float minZ, float maxX, float maxZ,
            TrianglesInBoundsScratch& scratch
        );

    private:

    };

}