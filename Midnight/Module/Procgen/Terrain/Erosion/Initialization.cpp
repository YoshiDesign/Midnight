#include "Initialization.h"
#include <span>
#include <cassert>
#include "Module/Procgen/Rng.h"
#include "Module/Procgen/Noise/Functions.h"

namespace {
    float seedOffset01(uint64_t& s) {
        // Convert next uint64 -> [0,1). Any stable method is fine.
        // Using top 24 bits gives a clean mantissa-ish range.
        s = aveng::SplitMix64(s).next();
        const uint32_t top24 = uint32_t(s >> 40);
        return float(top24) / float(1u << 24);
    }
}

namespace procgen {

    void ComputeHardnessMap(
        std::span<float> outHardness,
        std::span<const aveng::Vec2> positions,
        std::span<const float> heights,
        const aveng::HardnessParams& cfg,
        uint64_t seed
    ) {
        const size_t N = heights.size();
        assert(outHardness.size() == N);
        assert(positions.size() >= N);

        //if (!cfg.Enabled) {
        //    std::fill(outHardness.begin(), outHardness.end(), 0.0f);
        //    return;
        //}

        // 1) Min/max (serial for determinism + simplicity)
        float minH = heights[0], maxH = heights[0];
        for (size_t i = 1; i < N; ++i) {
            minH = std::min(minH, heights[i]);
            maxH = std::max(maxH, heights[i]);
        }

        const float range = maxH - minH;
        if (range < 1e-9f) {
            std::fill(outHardness.begin(), outHardness.end(), cfg.BaseHardness);
            return;
        }

        // 2) Seeded domain offsets for fixed-perm simplex
        uint64_t s = seed;
        const float ox = seedOffset01(s) * 10000.0f; // world-units offset
        const float oy = seedOffset01(s) * 10000.0f;

        const float totalW = cfg.ElevationWeight + cfg.NoiseWeight;
        const float invTotalW = (totalW > 0.0f) ? (1.0f / totalW) : 0.0f;

        for (size_t i = 0; i < N; ++i) {
            const float h = heights[i];
            const float normalized = (h - minH) / range;

            const double elevFactor = std::pow(double(normalized), double(cfg.ElevationPower));

            double noiseFactor = 0.0;
            {
                const float x = (positions[i].x + ox) * cfg.NoiseFrequency;
                const float y = (positions[i].y + oy) * cfg.NoiseFrequency;

                const double n = double(aveng::noise::Simplex2D(x, y)); // [-1,1]
                noiseFactor = (n + 1.0) * 0.5;            // [0,1]
            }

            double weighted = elevFactor * double(cfg.ElevationWeight)
                + noiseFactor * double(cfg.NoiseWeight);

            if (invTotalW > 0.0f) weighted *= double(invTotalW);

            double hardness = double(cfg.BaseHardness)
                + weighted * (1.0 - double(cfg.BaseHardness));

            hardness = std::min(1.0, std::max(0.0, hardness));
            outHardness[i] = float(hardness);
        }
    }

}