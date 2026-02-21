#pragma once
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

/*
& From ChatGuy:
& 3) Two important notes for Midnight
&    A) Determinism vs the fixed permutation table
&
&    Your Go version uses a fixed perm table. That means:
&        terrain is deterministic across machines (good),
&        but not keyed by world seed (every world looks the same unless you offset coordinates or layer other seeded functions).
&    If you want different worlds per worldSeed, the usual approach is:
&        build a perm[512] table from seed (Fisher–Yates shuffle of 0..255, then duplicate),
&        keep it per "noise context" object (or per chunk if you must, but per world is best).
&    If that’s on your roadmap, I can provide a Simplex2DSeeded object that stores the perm table and is still very fast.
&
&    B) Your FastFloor exactness
&        I matched your Go fastFloor behavior exactly. That helps parity tests.
&        Later you may prefer std::floor or a safer branchless version, but this one is fine and predictable.
*/

namespace aveng::noise {

    // If you want float everywhere (faster), flip to float.
    // Using double keeps parity with your Go prototype.
    using Real = float;

    // --- Constants (2D skew/unskew)
    // F2 = 0.5 * (sqrt(3) - 1)
    // G2 = (3 - sqrt(3)) / 6
    inline constexpr Real kF2 = Real(0.5) * (Real(1.7320508075688772935) - Real(1.0));
    inline constexpr Real kG2 = (Real(3.0) - Real(1.7320508075688772935)) / Real(6.0);

    // --- 12 gradient directions (same as your Go)
    inline constexpr std::array<std::array<Real, 2>, 12> kGrad2 = { {
        {{ 1,  1}}, {{-1,  1}}, {{ 1, -1}}, {{-1, -1}},
        {{ 1,  0}}, {{-1,  0}}, {{ 0,  1}}, {{ 0, -1}},
        {{ 1,  1}}, {{-1,  1}}, {{ 1, -1}}, {{-1, -1}},
    } };

    // --- Permutation table (512) (same values as your Go, duplicated)
    // NOTE: This is the classic "fixed perm" variant. If you later want seeded noise,
    // we can generate a perm table per-worldSeed instead.
    inline constexpr std::array<int, 512> kPerm = { {
        151, 160, 137,  91,  90,  15, 131,  13, 201,  95,  96,  53, 194, 233,   7, 225,
        140,  36, 103,  30,  69, 142,   8,  99,  37, 240,  21,  10,  23, 190,   6, 148,
        247, 120, 234,  75,   0,  26, 197,  62,  94, 252, 219, 203, 117,  35,  11,  32,
         57, 177,  33,  88, 237, 149,  56,  87, 174,  20, 125, 136, 171, 168,  68, 175,
         74, 165,  71, 134, 139,  48,  27, 166,  77, 146, 158, 231,  83, 111, 229, 122,
         60, 211, 133, 230, 220, 105,  92,  41,  55,  46, 245,  40, 244, 102, 143,  54,
         65,  25,  63, 161,   1, 216,  80,  73, 209,  76, 132, 187, 208,  89,  18, 169,
        200, 196, 135, 130, 116, 188, 159,  86, 164, 100, 109, 198, 173, 186,   3,  64,
         52, 217, 226, 250, 124, 123,   5, 202,  38, 147, 118, 126, 255,  82,  85, 212,
        207, 206,  59, 227,  47,  16,  58,  17, 182, 189,  28,  42, 223, 183, 170, 213,
        119, 248, 152,   2,  44, 154, 163,  70, 221, 153, 101, 155, 167,  43, 172,   9,
        129,  22,  39, 253,  19,  98, 108, 110,  79, 113, 224, 232, 178, 185, 112, 104,
        218, 246,  97, 228, 251,  34, 242, 193, 238, 210, 144,  12, 191, 179, 162, 241,
         81,  51, 145, 235, 249,  14, 239, 107,  49, 192, 214,  31, 181, 199, 106, 157,
        184,  84, 204, 176, 115, 121,  50,  45, 127,   4, 150, 254, 138, 236, 205,  93,
        222, 114,  67,  29,  24,  72, 243, 141, 128, 195,  78,  66, 215,  61, 156, 180,

        // duplicate:
        151, 160, 137,  91,  90,  15, 131,  13, 201,  95,  96,  53, 194, 233,   7, 225,
        140,  36, 103,  30,  69, 142,   8,  99,  37, 240,  21,  10,  23, 190,   6, 148,
        247, 120, 234,  75,   0,  26, 197,  62,  94, 252, 219, 203, 117,  35,  11,  32,
         57, 177,  33,  88, 237, 149,  56,  87, 174,  20, 125, 136, 171, 168,  68, 175,
         74, 165,  71, 134, 139,  48,  27, 166,  77, 146, 158, 231,  83, 111, 229, 122,
         60, 211, 133, 230, 220, 105,  92,  41,  55,  46, 245,  40, 244, 102, 143,  54,
         65,  25,  63, 161,   1, 216,  80,  73, 209,  76, 132, 187, 208,  89,  18, 169,
        200, 196, 135, 130, 116, 188, 159,  86, 164, 100, 109, 198, 173, 186,   3,  64,
         52, 217, 226, 250, 124, 123,   5, 202,  38, 147, 118, 126, 255,  82,  85, 212,
        207, 206,  59, 227,  47,  16,  58,  17, 182, 189,  28,  42, 223, 183, 170, 213,
        119, 248, 152,   2,  44, 154, 163,  70, 221, 153, 101, 155, 167,  43, 172,   9,
        129,  22,  39, 253,  19,  98, 108, 110,  79, 113, 224, 232, 178, 185, 112, 104,
        218, 246,  97, 228, 251,  34, 242, 193, 238, 210, 144,  12, 191, 179, 162, 241,
         81,  51, 145, 235, 249,  14, 239, 107,  49, 192, 214,  31, 181, 199, 106, 157,
        184,  84, 204, 176, 115, 121,  50,  45, 127,   4, 150, 254, 138, 236, 205,  93,
        222, 114,  67,  29,  24,  72, 243, 141, 128, 195,  78,  66, 215,  61, 156, 180,
    } };

    // --- Helpers
    [[nodiscard]] inline constexpr Real Dot2(const std::array<Real, 2>& g, Real x, Real y) noexcept {
        return g[0] * x + g[1] * y;
    }

    // Match your Go fastFloor exactly.
    [[nodiscard]] inline int FastFloor(Real x) noexcept {
        // For x > 0: trunc is floor. For x <= 0: trunc is ceil, so subtract 1.
        const int xi = static_cast<int>(x);
        return (x > Real(0)) ? xi : (xi - 1);
    }

    // 2D Simplex noise in [-1, 1] (approximately; exact scaling depends on constants)
    [[nodiscard]] inline Real Simplex2D(Real x, Real z) noexcept {
        Real n0{}, n1{}, n2{};

        // Skew the input space to determine which simplex cell we're in
        const Real s = (x + z) * kF2;
        const int i = FastFloor(x + s);
        const int j = FastFloor(z + s);

        // Unskew the cell origin back to (x, z) space
        const Real t = Real(i + j) * kG2;
        const Real X0 = Real(i) - t;
        const Real Y0 = Real(j) - t;
        const Real x0 = x - X0;
        const Real y0 = z - Y0;

        // Determine which simplex we’re in
        int i1, j1;
        if (x0 > y0) { i1 = 1; j1 = 0; }
        else { i1 = 0; j1 = 1; }

        // Offsets for middle corner
        const Real x1 = x0 - Real(i1) + kG2;
        const Real y1 = y0 - Real(j1) + kG2;

        // Offsets for last corner
        const Real x2 = x0 - Real(1.0) + Real(2.0) * kG2;
        const Real y2 = y0 - Real(1.0) + Real(2.0) * kG2;

        // Hash gradient indices
        const int ii = i & 255;
        const int jj = j & 255;

        const int gi0 = kPerm[ii + kPerm[jj]] % 12;
        const int gi1 = kPerm[ii + i1 + kPerm[jj + j1]] % 12;
        const int gi2 = kPerm[ii + 1 + kPerm[jj + 1]] % 12;

        // Contribution from corner 0
        Real t0 = Real(0.5) - x0 * x0 - y0 * y0;
        if (t0 > Real(0)) {
            t0 *= t0;
            n0 = t0 * t0 * Dot2(kGrad2[gi0], x0, y0);
        }

        // Corner 1
        Real t1 = Real(0.5) - x1 * x1 - y1 * y1;
        if (t1 > Real(0)) {
            t1 *= t1;
            n1 = t1 * t1 * Dot2(kGrad2[gi1], x1, y1);
        }

        // Corner 2
        Real t2 = Real(0.5) - x2 * x2 - y2 * y2;
        if (t2 > Real(0)) {
            t2 *= t2;
            n2 = t2 * t2 * Dot2(kGrad2[gi2], x2, y2);
        }

        // Scale to ~[-1,1]
        return Real(70.0) * (n0 + n1 + n2);
    }

    // Fractal noise: sum octave(Simplex2D(x*freq, z*freq) * amp), then update amp/freq
    [[nodiscard]] inline Real FractalNoiseV2(
        Real x, Real z,
        NoiseParams np
        // int octaves,
        // Real frequency,
        // Real amplitude,
        // Real persistence,
        //Real lacunarity
    ) noexcept {
        Real sum = Real(0);
        for (int i = 0; i < np.octaves; ++i) {
            sum += Simplex2D(x * np.frequency, z * np.frequency) * np.amplitude;
            np.amplitude *= np.persistence;
            np.frequency *= np.lacunarity;
        }
        return sum;
    }

} // namespace aveng::noise