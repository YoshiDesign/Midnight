#include "Scratch.h"
#include "Module/Procgen/Types.h" // For ChunkArena
#include "Runtime/Memory/ChunkArena.h"

namespace aveng {
    thread_local ChunkArena tlsScratch;

    ChunkArena& tlsScratchArena() {
        return tlsScratch;
    }
}