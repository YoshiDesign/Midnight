#pragma once

namespace aveng {

    /* Linear Allocator */
    struct Arena {
        std::byte* base_ptr;          // Start of the reservation
        size_t reserved_size;    // Total size
        size_t committed_size;   // Currently committed memory
        size_t current_offset;   // Bump pointer
    };

	Arena* CreateArena(size_t size);
    void* ArenaAlloc(Arena* arena, size_t size);
    void ArenaReset(Arena* arena);
    void DestroyArena(Arena* arena);

}