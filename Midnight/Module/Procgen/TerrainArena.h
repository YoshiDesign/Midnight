#pragma once
#include <cstdint>
#include <cstddef>
#include "Core/Math/Vector.h"

namespace {

    inline std::size_t next_pow2(std::size_t x) {

        if (x == 0) return 1;
        --x;
        x |= x >> 1; x |= x >> 2; x |= x >> 4;
        x |= x >> 8; x |= x >> 16;
#if INTPTR_MAX == INT64_MAX
        x |= x >> 32;
#endif
        return x + 1;
    }

}

namespace procgen {

    const uint32_t MAX_CHUNK_RECORDS = 36;

    /* Final allocation constants - 8 MB per slot */
    constexpr uint32_t kFinalSlots = MAX_CHUNK_RECORDS;
    constexpr size_t   kFinalBytesPerSlot = 8 * 1024 * 1024;
    constexpr size_t   kFinalReserveSize = kFinalSlots * kFinalBytesPerSlot; // unused (just illustrative)

    /* Scratch allocation constants - 8 MB per slot */
    constexpr uint32_t kScratchSlots = MAX_CHUNK_RECORDS;
    constexpr size_t   kScratchBytesPerSlot = 8 * 1024 * 1024;
    constexpr size_t   kScratchReserveSize = kScratchSlots * kScratchBytesPerSlot; // unused (just illustrative)
    
    /* Offsets into Arena (Unused) */
    constexpr size_t kScratch_offset = 0;
	constexpr size_t kFinal_offset = kScratch_offset + kScratchReserveSize;


    /* Init via: 
     *   scratch[i].base     = globalBase + i * kScratchBytesPerSlot;
     *   scratch[i].capacity = kScratchBytesPerSlot;
     *   scratch[i].offset   = 0;
     */

    struct ScratchArena {
        std::byte* base{};
        uint32_t capacity{};
        uint32_t offset{};
    };

    inline void ScratchReset(ScratchArena& scratch) noexcept {
        scratch.offset = 0;
    }

    template<typename T>
    T* ScratchAlloc(ScratchArena& scratch, uint32_t count = 1, size_t alignment = alignof(T)) noexcept {

        

#ifdef M_DEBUG
        assert(scratch.base != nullptr);
        assert(alignment != 0 && (alignment & (alignment - 1)) == 0);
#endif
        const uintptr_t base = reinterpret_cast<uintptr_t>(scratch.base);
        const uintptr_t current = base + static_cast<uintptr_t>(scratch.offset);
        const uintptr_t aligned = (current + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);

        const size_t bytes = sizeof(T) * static_cast<size_t>(count);
        const size_t newOffset = static_cast<size_t>(aligned - base) + bytes;

        if (newOffset > scratch.capacity) {
            return nullptr;
        }

        scratch.offset = static_cast<uint32_t>(newOffset);
        return reinterpret_cast<T*>(aligned);
    }

    /* Helper Types */

    struct PointsRange {
		aveng::Vec2* points  = nullptr;
        uint32_t points_size = 0;
    };

}