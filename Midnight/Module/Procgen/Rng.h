#pragma once
#include <cstdint>
#include <limits>
#include <type_traits>

namespace SeedTag {
    // Arbitrary, but keep these stable or the world will never be the same.
    static constexpr uint64_t Hardness = 0xA1B2C3D4E5F60718ull;
    static constexpr uint64_t Hydraulic = 0xBADC0FFEE0DDF00Dull;
    static constexpr uint64_t Thermal = 0x123456789ABCDEF0ull;
    static constexpr uint64_t Ridge = 0x0F1E2D3C4B5A6978ull;
    static constexpr uint64_t Smooth = 0xC001D00DC0FFEE11ull;
}

namespace aveng {

    // ------------------------------------------------------------
    // SplitMix64: used only for seeding other RNGs
    // ------------------------------------------------------------
    struct SplitMix64 {
        uint64_t x;
    
        explicit SplitMix64(uint64_t seed) : x(seed) {}
    
        uint64_t next() {
            uint64_t z = (x += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }
    };

    // Mix baseSeed + tag into a new 64-bit seed.
    // Deterministic, cheap, good avalanche. The opposite of rng, 
    // but that's a correlation at the end of the day so it can live here.
    static inline uint64_t stageSeed(uint64_t baseSeed, uint64_t tag) {
        // SplitMix64 is commonly used exactly for this purpose.
        SplitMix64 sm(baseSeed ^ tag);
        return sm.next();
    }
    
    // ------------------------------------------------------------
    // xoroshiro256**
    // Reference: Blackman & Vigna (public domain reference impl)
    // Great for procedural generation (not cryptographic).
    // ------------------------------------------------------------
    struct Rng {
        uint64_t s[4] = {0, 0, 0, 0};
    };
    
    inline uint64_t rotl_u64(uint64_t x, int k) {
        return (x << k) | (x >> (64 - k));
    }
    
    // Seed from a single 64-bit seed into a full 256-bit state.
    // IMPORTANT: State must not be all zeros; SplitMix64 makes that vanishingly unlikely,
    // but we still defensively guard it.
    inline void SeedRng(Rng& rng, uint64_t seed) {
        SplitMix64 sm(seed);
    
        rng.s[0] = sm.next();
        rng.s[1] = sm.next();
        rng.s[2] = sm.next();
        rng.s[3] = sm.next();
    
        // Defensive: avoid forbidden all-zero state
        if ((rng.s[0] | rng.s[1] | rng.s[2] | rng.s[3]) == 0ull) {
            rng.s[0] = 0x9E3779B97F4A7C15ull;
            rng.s[1] = 0xBF58476D1CE4E5B9ull;
            rng.s[2] = 0x94D049BB133111EBull;
            rng.s[3] = 0xD2B74407B1CE6E93ull;
        }
    }
    
    // Core 64-bit output
    inline uint64_t NextU64(Rng& r) {
        // xoroshiro256** output function
        const uint64_t result = rotl_u64(r.s[1] * 5ull, 7) * 9ull;
    
        const uint64_t t = r.s[1] << 17;
    
        r.s[2] ^= r.s[0];
        r.s[3] ^= r.s[1];
        r.s[1] ^= r.s[2];
        r.s[0] ^= r.s[3];
    
        r.s[2] ^= t;
        r.s[3] = rotl_u64(r.s[3], 45);
    
        return result;
    }
    
    // Uniform float in [0,1). Uses 24 bits (good for float precision).
    inline float Uniform01(Rng& r) {
        // Take the top 24 bits -> [0, 2^24)
        const uint32_t x = static_cast<uint32_t>(NextU64(r) >> 40);
        // Divide by 2^24
        return static_cast<float>(x) * (1.0f / 16777216.0f);
    }
    
    // Uniform float in [0,1). Uses 53 bits (good for float precision).
    inline float Uniform01d(Rng& r) {
        // Take top 53 bits and scale to [0,1)
        const uint64_t x = NextU64(r) >> 11;
        return static_cast<float>(x) * (1.0 / 9007199254740992.0); // 2^53
    }
    
    // Uniform float in [minVal, maxVal)
    inline float UniformRange(Rng& r, float minVal, float maxVal) {
        return minVal + (maxVal - minVal) * Uniform01(r);
    }
    
    // Uniform integer in [0, bound) without modulo bias (rejection sampling).
    // Works for any unsigned bound > 0.
    inline uint32_t UniformU32Bounded(Rng& r, uint32_t bound) {
        // Daniel Lemire fast range reduction (with rejection)
        // https://arxiv.org/abs/1805.10941
        uint64_t x, m;
        uint32_t l;
    
        do {
            x = static_cast<uint32_t>(NextU64(r));     // 32 random bits
            m = x * static_cast<uint64_t>(bound);
            l = static_cast<uint32_t>(m);
        } while (l < bound && l < ((~bound + 1u) % bound)); // rejection threshold
    
        return static_cast<uint32_t>(m >> 32);
    }
    
    // Uniform integer in [minVal, maxVal] inclusive (signed/unsigned)
    // Note: inclusive max is often convenient for "pick an index".
    template <typename IntT>
    inline IntT UniformInt(Rng& r, IntT minVal, IntT maxVal) {
        static_assert(std::is_integral_v<IntT>, "UniformInt requires integral type");
        using Uns = std::make_unsigned_t<IntT>;
    
        if (maxVal < minVal) {
            IntT tmp = minVal;
            minVal = maxVal;
            maxVal = tmp;
        }
    
        const Uns span = static_cast<Uns>(maxVal - minVal) + static_cast<Uns>(1);
        // If span fits in u32, use bounded u32
        if (span <= std::numeric_limits<uint32_t>::max()) {
            const uint32_t v = UniformU32Bounded(r, static_cast<uint32_t>(span));
            return static_cast<IntT>(static_cast<Uns>(minVal) + static_cast<Uns>(v));
        }
    
        // Fallback for very large spans: rejection on 64-bit
        while (true) {
            const uint64_t x = NextU64(r);
            const Uns v = static_cast<Uns>(x); // truncation is okay for rejection
            // Rejection threshold to avoid modulo bias for huge spans:
            const Uns threshold = (Uns(0) - span) % span;
            if (v >= threshold) {
                return static_cast<IntT>(static_cast<Uns>(minVal) + (v % span));
            }
        }
    }
    

}