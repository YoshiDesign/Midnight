#pragma once
#include <bit>
#include <cmath>
#include <cstdint>
// #include <xmmintrin.h>

namespace aveng {

    // Helper clamp for ints - I can't say I mind the use of static here
    static inline int clampInt(int v, int lo, int hi) {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    struct BaryWeights {
        float wa = 0.f, wb = 0.f, wc = 0.f;
    };

    // Unused, mostly for me to review and understand where __SSE__ comes from
    inline float rsqrt_fast(float x) {
#if defined(__SSE__)
        __m128 v = _mm_set_ss(x);
        v = _mm_rsqrt_ss(v);
        // Optional: 1 Newton step for better accuracy
        // v = v * (1.5f - 0.5f*x*v*v);
        return _mm_cvtss_f32(v);
#else
        return 1.0f / std::sqrt(x);
#endif
    }

    // Also Unused, here to study. Not quite as fast as Carmack's inv square root from Quake III Arena
    // This is apparently portable and without any intrinsics.
    // HOWEVER - You never know, our compiler could already be optimizing our inv square roots for us.
    // Nice time to inspect disassembly or profile
    inline float q_rsqrt(float x) {
        // Initial approximation
        float xhalf = 0.5f * x;
        uint32_t i = std::bit_cast<uint32_t>(x);
        i = 0x5f3759dfu - (i >> 1);
        float y = std::bit_cast<float>(i);

        // 1 Newton step (usually enough for normalization)
        y = y * (1.5f - xhalf * y * y);
        return y;
    }

    // For studying intrinsics and comparing disassembly to less SIMD friendly approaches
    //inline float rsqrt_fast_SIMD(float x) {
    //    __m128 v = _mm_set_ss(x);
    //    __m128 r = _mm_rsqrt_ss(v);

    //    const __m128 half = _mm_set_ss(0.5f);
    //    const __m128 onep5 = _mm_set_ss(1.5f);

    //    __m128 r2 = _mm_mul_ss(r, r);
    //    __m128 x_r2 = _mm_mul_ss(v, r2);
    //    __m128 term = _mm_sub_ss(onep5, _mm_mul_ss(half, x_r2));
    //    r = _mm_mul_ss(r, term);

    //    return _mm_cvtss_f32(r);
    //}

}