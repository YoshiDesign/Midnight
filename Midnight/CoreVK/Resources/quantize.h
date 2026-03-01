#pragma once
#include <cmath>
#include <cstdint>
#include "CoreVK/Resources/wyhash.h"
#include "Core/Math/Vector.h"
namespace aveng {

    // Quantize to a grid for stable dedupe (works well for floating point).
    // eps should be smaller than your minimum point spacing (e.g. 1e-3 or 1e-4 * world units),
    // and much smaller than any meaningful distances in your simulation.
    struct QKey {
        int64_t qx{};
        int64_t qz{};
        friend bool operator==(QKey a, QKey b) noexcept { return a.qx == b.qx && a.qz == b.qz; }
    };

    struct QKeyHash {
        size_t operator()(QKey k) const noexcept {
            const uint64_t a = static_cast<uint64_t>(k.qx);
            const uint64_t b = static_cast<uint64_t>(k.qz);
            const uint64_t h = wyhash64(a, b);
            return static_cast<size_t>(h);
        }
    };

    static inline int64_t quantize1(float v, float invEps) noexcept {
        // Round-to-nearest, ties go away from zero-ish depending on fractional part.
        // Deterministic and fast.
        const float x = v * invEps;
        if (x >= 0.0f) {
            return (int64_t)std::floor((double)x + 0.5);
        }
        else {
            return (int64_t)std::ceil((double)x - 0.5);
        }
    }

    inline QKey quantizeFast(const Vec2& p, float eps) noexcept {
        const float inv = 1.0f / eps;
        return QKey{
            quantize1(p.x, inv),
            quantize1(p.y, inv) // (or p.z depending on your Vec2)
        };
    }

}