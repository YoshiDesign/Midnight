#include "Bluenoise.h"
#include "Module/Procgen/Rng.h"
namespace aveng {

    // ---------------------------
    // FNV-1a 64-bit (Go fnv.New64a parity)
    // ---------------------------
    static inline uint64_t fnv1a64_init() {
        return 14695981039346656037ull; // offset basis
    }

    static inline uint64_t fnv1a64_prime() {
        return 1099511628211ull;
    }

    static inline void fnv1a64_write_u8(uint64_t& h, uint8_t b) {
        h ^= static_cast<uint64_t>(b);
        h *= fnv1a64_prime();
    }

    static inline void fnv1a64_write_bytes(uint64_t& h, const uint8_t* data, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            fnv1a64_write_u8(h, data[i]);
        }
    }

    // This matches the prototype's byte-writing behavior exactly:
    // - worldSeed written as 8 bytes little-endian
    // - coord.X written as 4 bytes little-endian + 4 zeros
    // - coord.Z written as 4 bytes little-endian (and remaining bytes unused)
    int64_t ChunkSeed(int64_t worldSeed, ChunkCoord coord) {
        uint64_t h = fnv1a64_init();

        // Write worldSeed: 8 bytes little-endian
        {
            uint64_t ws = static_cast<uint64_t>(worldSeed);
            uint8_t buf[8];
            buf[0] = static_cast<uint8_t>(ws);
            buf[1] = static_cast<uint8_t>(ws >> 8);
            buf[2] = static_cast<uint8_t>(ws >> 16);
            buf[3] = static_cast<uint8_t>(ws >> 24);
            buf[4] = static_cast<uint8_t>(ws >> 32);
            buf[5] = static_cast<uint8_t>(ws >> 40);
            buf[6] = static_cast<uint8_t>(ws >> 48);
            buf[7] = static_cast<uint8_t>(ws >> 56);
            fnv1a64_write_bytes(h, buf, 8);
        }

        // Write chunk X: 4 bytes little-endian + 4 zeros
        {
            uint32_t x = static_cast<uint32_t>(coord.X);
            uint8_t buf[8];
            buf[0] = static_cast<uint8_t>(x);
            buf[1] = static_cast<uint8_t>(x >> 8);
            buf[2] = static_cast<uint8_t>(x >> 16);
            buf[3] = static_cast<uint8_t>(x >> 24);
            buf[4] = 0;
            buf[5] = 0;
            buf[6] = 0;
            buf[7] = 0;
            fnv1a64_write_bytes(h, buf, 8);
        }

        // Write chunk Z: 4 bytes little-endian
        {
            uint32_t z = static_cast<uint32_t>(coord.Z);
            uint8_t buf[4];
            buf[0] = static_cast<uint8_t>(z);
            buf[1] = static_cast<uint8_t>(z >> 8);
            buf[2] = static_cast<uint8_t>(z >> 16);
            buf[3] = static_cast<uint8_t>(z >> 24);
            fnv1a64_write_bytes(h, buf, 4);
        }

        return static_cast<int64_t>(h);
    }

    // ---------------------------
    // Blue noise (Bridson's Algorithm)
    // ---------------------------
    std::vector<Vec2> GenerateBlueNoise(
        Rng& rng,
        float minX, float minZ,
        float maxX, float maxZ,
        BlueNoiseConfig cfg
    ) {
        if (cfg.MinDist <= 0.0) {
            return {};
        }
        if (cfg.MaxTries <= 0) {
            cfg.MaxTries = 30;
        }

        const float width  = maxX - minX;
        const float height = maxZ - minZ;
        if (width <= 0.0 || height <= 0.0) {
            return {};
        }

        // Cell size = r / sqrt(2)
        const float cellSize = cfg.MinDist / std::sqrt(2.0);
        const int gridW = static_cast<int>(std::ceil(width / cellSize));
        const int gridH = static_cast<int>(std::ceil(height / cellSize));
        if (gridW <= 0 || gridH <= 0) {
            return {};
        }

        // Grid stores index into points, or -1 if empty
        std::vector<int> grid(static_cast<size_t>(gridW) * static_cast<size_t>(gridH), -1);

        std::vector<Vec2> points;
        points.reserve(static_cast<size_t>(gridW) * static_cast<size_t>(gridH) / 4);

        std::vector<int> active;
        active.reserve(128);

        auto clampi = [](int v, int lo, int hi) -> int {
            return (v < lo) ? lo : (v > hi) ? hi : v;
        };

        // Convert world coords to grid cell (clamped)
        auto toGrid = [&](const Vec2& p) -> std::pair<int,int> {
            int gx = static_cast<int>((p.x - minX) / cellSize);
            int gz = static_cast<int>((p.y - minZ) / cellSize);

            gx = clampi(gx, 0, gridW - 1);
            gz = clampi(gz, 0, gridH - 1);
            return {gx, gz};
        };

        auto isValid = [&](const Vec2& p) -> bool {
            if (p.x < minX || p.x >= maxX || p.y < minZ || p.y >= maxZ) {
                return false;
            }

            const auto [gx, gz] = toGrid(p);

            const float r2 = cfg.MinDist * cfg.MinDist;

            // Check 5x5 neighborhood (-2..2) due to r/sqrt(2) cell size
            for (int dz = -2; dz <= 2; ++dz) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int nx = gx + dx;
                    const int nz = gz + dz;
                    if (nx < 0 || nx >= gridW || nz < 0 || nz >= gridH) {
                        continue;
                    }
                    const int idx = grid[static_cast<size_t>(nz) * static_cast<size_t>(gridW) + static_cast<size_t>(nx)];
                    if (idx != -1) {
                        const Vec2 diff = points[static_cast<size_t>(idx)] - p;
                        if (diff.len2() < r2) {
                            return false;
                        }
                    }
                }
            }
            return true;
        };

        auto insert = [&](const Vec2& p) {
            const int idx = static_cast<int>(points.size());
            points.push_back(p);
            active.push_back(idx);

            const auto [gx, gz] = toGrid(p);
            grid[static_cast<size_t>(gz) * static_cast<size_t>(gridW) + static_cast<size_t>(gx)] = idx;
        };

        // Start with a random initial point
        const float startX = minX + Uniform01d(rng) * width;
        const float startZ = minZ + Uniform01d(rng) * height;
        insert(Vec2{startX, startZ});

        constexpr float TWO_PI = 6.2831855f;

        while (!active.empty()) {
            // Pick random active point index: ai in [0, len(active)-1]
            const int ai = UniformInt(rng, 0, static_cast<int>(active.size()) - 1);
            const int pi = active[static_cast<size_t>(ai)];
            const Vec2 p = points[static_cast<size_t>(pi)];

            bool found = false;

            for (int k = 0; k < cfg.MaxTries; ++k) {
                const float angle = Uniform01d(rng) * TWO_PI;
                const float dist  = cfg.MinDist + Uniform01d(rng) * cfg.MinDist; // [r, 2r)
                const Vec2 candidate{
                    p.x + dist * std::cos(angle),
                    p.y + dist * std::sin(angle)
                };

                if (isValid(candidate)) {
                    insert(candidate);
                    found = true;
                    break;
                }
            }

            if (!found) {
                // swap-remove active[ai]
                active[static_cast<size_t>(ai)] = active.back();
                active.pop_back();
            }
        }

        return points;
    }

    std::vector<Vec2> GenerateBlueNoiseSeeded(
        int64_t seed,
        float minX, float minZ,
        float maxX, float maxZ,
        BlueNoiseConfig cfg
    ) {
        Rng rng{};
        SeedRng(rng, static_cast<uint64_t>(seed));
        return GenerateBlueNoise(rng, minX, minZ, maxX, maxZ, cfg);
    }

}