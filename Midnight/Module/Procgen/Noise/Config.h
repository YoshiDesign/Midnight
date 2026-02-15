#pragma once

namespace aveng
{
    // -------------------------
    // Terrain config
    // -------------------------
    struct NoiseParams {
        int   octaves = 6;
        float frequency = 0.01f;
        float amplitude = 1.0f;
        float persistence = 0.5f;
        float lacunarity = 2.0f;
    };
}