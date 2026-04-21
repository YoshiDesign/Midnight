#include "Arena.h"
#include <cstddef>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif

// Dynamically get the OS page size once.
const size_t PAGE_SIZE = []() {
#ifdef _WIN32
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    return sysInfo.dwPageSize;
#else
    return (size_t)sysconf(_SC_PAGESIZE);
#endif
}();

namespace aveng {

    /* Create a Linear Allocator */
    Arena* CreateArena(size_t size) {
        Arena* arena = (Arena*)malloc(sizeof(Arena));
        if (!arena) return NULL;

        // Properly align the size (size must be a power of 2!!)
		size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

#ifdef _WIN32
        void* block = VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_NOACCESS);
        if (!block) {
            free(arena);
            return NULL;
        }
#else
        void* block = mmap(NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (block == MAP_FAILED) {
            free(arena);
            return nullptr;
        }
#endif

        arena->base_ptr = (std::byte*)block;
        arena->reserved_size = size;
        arena->committed_size = 0;
        arena->current_offset = 0;

        return arena;
    }
	
    /* Allocate memory from the arena */
    void* ArenaAlloc(Arena* arena, size_t size) {
        if (!arena || size == 0) return nullptr;

        size_t new_offset = arena->current_offset + size;

        // Beyond our reserved address space
        if (new_offset > arena->reserved_size) {
            return NULL;
        }

		// Need to commit more memory to cover this allocation
        if (new_offset > arena->committed_size) {
            // Round up to the next page size - again, new_offset must be a power of 2
            size_t new_commit_target = (new_offset + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

            // Clamp if rounding exceeds reserved_size
            if (new_commit_target > arena->reserved_size) {
				new_commit_target = arena->reserved_size;
            }

            // Size of the new allocation
			size_t size_to_commit = new_commit_target - arena->committed_size;
            // Range of the new allocation
			void* commit_start_addr = arena->base_ptr + arena->committed_size;

#ifdef _WIN32
            // Extend the committed memory
            if (!VirtualAlloc(commit_start_addr, size_to_commit, MEM_COMMIT, PAGE_READWRITE)) {
                return NULL;
            }
#else
            if (mprotect(commit_start_addr, size_to_commit, PROT_READ | PROT_WRITE) != 0) {
                return NULL;
            }
#endif
            arena->committed_size = new_commit_target;

        }

        // Beginning of the new allocation
        void* memory = arena->base_ptr + arena->current_offset;
        // Bump
        arena->current_offset = new_offset;

        return memory;

    }

    //
    void ArenaReset(Arena* arena) {
        arena->current_offset = 0;
    }

    //
    void DestroyArena(Arena* arena) {

#ifdef _WIN32
        VirtualFree(arena->base_ptr, 0, MEM_RELEASE);
#else
        munmap(arena->base_ptr, arena->reserved_size);
#endif
        free(arena);
    }

    /* ** ** ** Scoped Arena ** ** ** */

    // A temporary "save point" for a parent arena
    struct ScopedArena {
        Arena* parent_arena;
        size_t original_offset;

        // Constructor saves the parent's current state
        ScopedArena(Arena* parent) {
            parent_arena = parent;
            original_offset = parent->current_offset;
        }

        // Destructor restores the parent's state, effectively "freeing"
        // all memory allocated during this scope's lifetime.
        ~ScopedArena() {
            parent_arena->current_offset = original_offset;
        }

        // We can even add an Alloc method for convenience
        void* Alloc(size_t size) {
            return ArenaAlloc(parent_arena, size);
        }
    };

}