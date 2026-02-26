#pragma once

namespace aveng::noise {
    // -------------------------
    // Terrain config
    // See Also: ChunkManager.h for `defaultNoiseParams()`
    // -------------------------
    struct NoiseParams {
        int   octaves = 7;
        float frequency = 0.01f;
        float amplitude = 1.0f;
        float persistence = 0.5f;
        float lacunarity = 2.0f;
    };

    struct BlueNoiseConfig {
        float MinDist = 0.0;
        int MaxTries = 30;
    };

}