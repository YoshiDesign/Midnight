#include "SpatialGrid.h"
#include "avpch.h"

namespace aveng {

    // Access: [cell][i]
    std::span<const uint32_t> SpatialGrid::trianglesForCell(uint32_t cell) const {
        uint32_t begin = cellOffsets[cell];
        uint32_t end   = cellOffsets[cell + 1];
        return { cellTriangles.data() + begin, end - begin };
    }

}