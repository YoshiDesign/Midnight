#include "Scratch.h"
#include "Runtime/Memory/ChunkArena.h"

namespace aveng {
    static constexpr size_t TLS_SCRATCH_SIZE = 8 * 1024 * 1024; // 8MB, matches worker thread reservation
    thread_local ChunkArena tlsScratch;

    ChunkArena& tlsScratchArena() {
        if (!tlsScratch.mr()) {
            tlsScratch.reserve(TLS_SCRATCH_SIZE);
        }
        return tlsScratch;
    }
}