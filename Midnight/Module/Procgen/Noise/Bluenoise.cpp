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
    // algorithm produces, then we don't need all of these static casts; just a style thing.
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
        if (cfg.MinDist <= 0.0f) {
            return {};
        }
        if (cfg.MaxTries <= 0) {
            cfg.MaxTries = 30;
        }

        const float width = maxX - minX;
        const float height = maxZ - minZ;
        if (width <= 0.0f || height <= 0.0f) {
            return {};
        }

        const float cellSize = cfg.MinDist / std::sqrt(2.0f);
        const int gridW = static_cast<int>(std::ceil(width / cellSize));
        const int gridH = static_cast<int>(std::ceil(height / cellSize));
        if (gridW <= 0 || gridH <= 0) {
            return {};
        }

        const size_t _w = static_cast<size_t>(gridW);
        const size_t _h = static_cast<size_t>(gridH);

        int* grid = procgen::ScratchAlloc<int>(mr, static_cast<uint32_t>(_w * _h));
        if (!grid) return {};

        const uint32_t maxPointCapacity = static_cast<uint32_t>(_w * _h / 4);
        Vec2* points = procgen::ScratchAlloc<Vec2>(mr, maxPointCapacity);
        if (!points) return {};

        int* active = procgen::ScratchAlloc<int>(mr, maxPointCapacity);
        if (!active) return {};

        uint32_t* binCounts = procgen::ScratchAlloc<uint32_t>(mr, procgen::BIN_COUNT);
        if (!binCounts) return {};

        uint8_t* binPerPoint = procgen::ScratchAlloc<uint8_t>(mr, maxPointCapacity);
        if (!binPerPoint) return {};

        for (size_t i = 0; i < _w * _h; ++i) {
            grid[i] = -1; // no memset?
        }
        for (int i = 0; i < procgen::BIN_COUNT; ++i) {
            binCounts[i] = 0; // no memset?
        }

        int points_idx = 0;
        int active_idx = 0;

        auto clampi = [](int v, int lo, int hi) -> int {
            return (v < lo) ? lo : (v > hi) ? hi : v;
        };

        auto toGrid = [&](const Vec2& p) -> std::pair<int, int> {
            int gx = static_cast<int>((p.x - minX) / cellSize);
            int gz = static_cast<int>((p.y - minZ) / cellSize);
            gx = clampi(gx, 0, gridW - 1);
            gz = clampi(gz, 0, gridH - 1);
            return { gx, gz };
        };

        auto isValid = [&](const Vec2& p) -> bool {
            if (p.x < minX || p.x >= maxX || p.y < minZ || p.y >= maxZ) {
                return false;
            }

            const auto [gx, gz] = toGrid(p);
            const float r2 = cfg.MinDist * cfg.MinDist;

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

        // halo thickness for export partitioning
        const float halo = width * (1.0f / 8.0f); // or pass this in explicitly
        const float leftBandMax = minX + halo;
        const float rightBandMin = maxX - halo;
        const float bottomBandMax = minZ + halo;
        const float topBandMin = maxZ - halo;

        auto classifyAxis = [](float v, float lo, float hi) noexcept -> int {
            return (v < lo) ? 0 : (v >= hi) ? 2 : 1;
        };

        auto insert = [&](const Vec2& p) {
#ifdef M_DEBUG
            assert(points_idx < static_cast<int>(maxPointCapacity) && "Bluenoise points[] overflowed");
            assert(active_idx < static_cast<int>(maxPointCapacity) && "Bluenoise active[] overflowed");
#endif
            if (points_idx >= static_cast<int>(maxPointCapacity) ||
                active_idx >= static_cast<int>(maxPointCapacity)) {
                return;
            }

            const int idx = points_idx;
            points[points_idx++] = p;
            active[active_idx++] = idx;

            const auto [gx, gz] = toGrid(p);
            grid[static_cast<size_t>(gz) * _w + static_cast<size_t>(gx)] = idx;

            const int xBand = classifyAxis(p.x, leftBandMax, rightBandMin);
            const int zBand = classifyAxis(p.y, bottomBandMax, topBandMin);
            const int bin = zBand * 3 + xBand; // 0..8

            binPerPoint[idx] = bin;
            ++binCounts[bin];
        };

        const float startX = minX + Uniform01d(rng) * width;
        const float startZ = minZ + Uniform01d(rng) * height;
        insert(Vec2{ startX, startZ });

        constexpr float TAU = 6.2831855f;

        while (active_idx > 0) {
            const int ai = UniformInt(rng, 0, active_idx - 1);
            const int pi = active[static_cast<size_t>(ai)];
            const Vec2 p = points[static_cast<size_t>(pi)];

            bool found = false;

            for (int k = 0; k < cfg.MaxTries; ++k) {
                const float angle = Uniform01d(rng) * TAU;
                const float dist = cfg.MinDist + Uniform01d(rng) * cfg.MinDist;

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
                active[static_cast<size_t>(ai)] = active[static_cast<size_t>(active_idx - 1)];
                --active_idx;
            }
        }

#ifdef M_DEBUG
        std::cout << "writing chunk data..." << std::endl;
        dumpChunkData(coord, points, points_idx);
#endif

        procgen::PointsRange out{};
        out.points = points;
        out.points_size = static_cast<uint32_t>(points_idx);
        out.binPerPoint = binPerPoint; // Used to classify into packed arena
        for (int i = 0; i < procgen::BIN_COUNT; ++i) {
            out.binCounts[i] = binCounts[i]; // ditto
        }
        return out;
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