#pragma once
#include <cstdint>
#include "Module/Procgen/Types.h"
#include "Utils/glm_includes.h"

namespace aveng {

    struct TerrainChunkGpuRange {
        uint32_t baseVertex;
        uint32_t vertexCount;

        uint32_t baseIndex;
        uint32_t indexCount;

        uint32_t baseTriangle;
        uint32_t triangleCount;

        uint32_t baseAdjacency;
        uint32_t adjacencyCount;

        uint32_t baseFaceNormal;
        uint32_t faceNormalCount;

        uint32_t baseWeight;
        uint32_t weightCount;

        uint32_t baseNormal;
        uint32_t normalCount;

        Bounds2 worldBounds;
        glm::ivec2 chunkCoord;
    };

}