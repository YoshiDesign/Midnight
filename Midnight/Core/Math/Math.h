#pragma once
namespace aveng {

    // Helper clamp for ints - I can't say I mind the use of static here
    static inline int clampInt(int v, int lo, int hi) {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

    struct BaryWeights {
        float wa = 0.f, wb = 0.f, wc = 0.f;
    };

}