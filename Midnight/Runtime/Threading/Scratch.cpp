#include "Scratch.h"
#include "Runtime/Memory/ChunkArena.h"

namespace aveng {
    thread_local ChunkArena tlsScratch;

    ChunkArena& tlsScratchArena() {
        return tlsScratch;
    }
}