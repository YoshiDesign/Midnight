#include "Scratch.h"
#include "Module/Procgen/Types.h"   // where ChunkArena is fully defined

namespace aveng {
    thread_local ChunkArena tlsScratch;

    ChunkArena& tlsScratchArena() {
        return tlsScratch;
    }
}