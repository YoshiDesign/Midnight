#include "Scratch.h"
#include "Module/Procgen/Types.h" // For ChunkArena

namespace aveng {
    thread_local ChunkArena tlsScratch;

    ChunkArena& tlsScratchArena() {
        return tlsScratch;
    }
}