#pragma once
namespace aveng {

    // Helper clamp for ints
    static inline int clampInt(int v, int lo, int hi) {
        return (v < lo) ? lo : (v > hi) ? hi : v;
    }

}