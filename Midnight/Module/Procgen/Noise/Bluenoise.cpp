#include "Bluenoise.h"
#include "Runtime/Debug.h"
#include "Module/Procgen/Rng.h"
#ifdef M_DEBUG
#include <iostream>
#include <filesystem>
#endif
namespace aveng {

#ifdef M_DEBUG

    void dumpChunkData(procgen::ChunkCoord coord, Vec2* data, uint32_t data_size)
    {
        namespace fs = std::filesystem;

        fs::path exeDir = fs::current_path() / "dump";

        std::string name = std::to_string(coord.x) + "_" + std::to_string(coord.z);

        fs::path fullPath = exeDir /
            std::format("chunk_{}.txt", name);

        Debug::writeBlueNoiseDataToFile(fullPath, data, data_size);
    }
#endif


    // ---------------------------
    // Blue noise (Bridson's Algorithm)
    //
    // TODO: If indices are never conceptually negative in the grid which this
    // algorithm produces, then we don't need all of these static casts, perhaps.
    // ---------------------------
    procgen::PointsRange GenerateBlueNoise(
        Rng& rng,
        float minX, float minZ,
        float maxX, float maxZ,
        noise::BlueNoiseConfig cfg,
        procgen::ScratchArena& mr
#ifdef M_DEBUG
        , procgen::ChunkCoord coord
#endif
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

        // Since we index a lot - once gridW and gridH are validated...
        const size_t _w = static_cast<size_t>(gridW);
        const size_t _h = static_cast<size_t>(gridH);

        /// In case we need paranoid checks for overflow.
        // if (w != 0 && h > (std::numeric_limits<size_t>::max)() / w) {
        //     return {}; // would overflow size_t in w*h
        // }

        // Grid stores index into points, or -1 if empty
        int* grid = procgen::ScratchAlloc<int>(mr, static_cast<uint32_t>(_w * _h));
		int grid_size = _w * _h;
        int grid_idx = 0;

        Vec2* points = procgen::ScratchAlloc<Vec2>(mr, static_cast<uint32_t>(_w * _h / 4));
		int points_size = _w * _h / 4;
        int points_idx = 0;

		int* active = procgen::ScratchAlloc<int>(mr, static_cast<uint32_t>(128));
        int active_size = 128;
        int active_idx = 0;

        // This could just live inside of `toGrid` - Compiler will hopefully inline it until then
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
                    const int idx = grid[static_cast<size_t>(nz) * _w + static_cast<size_t>(nx)];
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

#ifdef M_DEBUG
            assert(points_idx < points_size && "Bluenoise points[] overflowed");
            assert(active_idx < active_size && "Bluenoise active[] overflowed");

            if (points_idx >= points_size) {
                return; // or assert(false), or handle failure
            }
            if (active_idx >= active_size) {
                return;
            }
#endif
            const int idx = points_idx;
            points[points_idx++] = p;
            active[active_idx++] = idx;

            const auto [gx, gz] = toGrid(p);
            grid[static_cast<size_t>(gz) * _w + static_cast<size_t>(gx)] = idx;
        };

        // Start with a random initial point
        const float startX = minX + Uniform01d(rng) * width;
        const float startZ = minZ + Uniform01d(rng) * height;
        insert(Vec2{startX, startZ});

        constexpr float TAU = 6.2831855f;

        //while (!active.empty()) {
        while (active_idx >= 0) {
            // Pick random active point index: ai in [0, len(active)-1]
            const int ai = UniformInt(rng, 0, static_cast<int>(active_idx) - 1);
            const int pi = active[static_cast<size_t>(ai)];
            const Vec2 p = points[static_cast<size_t>(pi)];

            bool found = false;

            for (int k = 0; k < cfg.MaxTries; ++k) {
                const float angle = Uniform01d(rng) * TAU;
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
                active[static_cast<size_t>(ai)] = active[active_idx - 1];
                active_idx--;
            }
        }

#ifdef M_DEBUG
        std::cout << "writing chunk data..." << std::endl;
        dumpChunkData(coord, points, points_idx);
#endif
        return { points, static_cast<uint32_t>(points_idx) };
    }

    procgen::PointsRange GenerateBlueNoiseSeeded(
        uint64_t seed,
        float minX, float minZ,
        float maxX, float maxZ,
        noise::BlueNoiseConfig cfg,
        procgen::ScratchArena& mr
#ifdef M_DEBUG
        , procgen::ChunkCoord coord
#endif
    ) {
        Rng rng{};
        SeedRng(rng, seed);
        return GenerateBlueNoise(rng, minX, minZ, maxX, maxZ, cfg, mr
#ifdef M_DEBUG
        , coord
#endif
        );
    }

}