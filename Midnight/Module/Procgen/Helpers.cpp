#include "Helpers.h"

namespace aveng {

    // ---------------------------
    // FNV-1a 64-bit (Go fnv.New64a parity)
    // ---------------------------
    constexpr uint64_t fnv1a64_init() { return 14695981039346656037ull; }  // offset basis

    constexpr uint64_t fnv1a64_prime() { return 1099511628211ull; }

    static void fnv1a64_write_u8(uint64_t& h, uint8_t b) {
        h ^= static_cast<uint64_t>(b);
        h *= fnv1a64_prime();
    }

    static void fnv1a64_write_bytes(uint64_t& h, const uint8_t* data, size_t n) {
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
            uint32_t x = static_cast<uint32_t>(coord.x);
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
            uint32_t z = static_cast<uint32_t>(coord.z);
            uint8_t buf[4];
            buf[0] = static_cast<uint8_t>(z);
            buf[1] = static_cast<uint8_t>(z >> 8);
            buf[2] = static_cast<uint8_t>(z >> 16);
            buf[3] = static_cast<uint8_t>(z >> 24);
            fnv1a64_write_bytes(h, buf, 4);
        }

        return static_cast<int64_t>(h);
    }

}