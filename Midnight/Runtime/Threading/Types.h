#pragma once
#include <memory>
#include <mutex>
#include <unordered_map>
#include "Module/Procgen/Types.h"

namespace aveng
{

    // Alternate way to implement striped mutex
    // template<size_t N>
    // struct StripedMutex {
    //     std::mutex m[N];
    //     std::mutex& forKey(size_t h) noexcept { return m[h % N]; }
    // };

    /**
     * Important note:
     * The bucket lock does not protect use of the ChunkRecord after 
     * you return the pointer. It only protects the map operation that 
     * finds/creates/stores the pointer.
     * 
     * A bucket lock is taken for operations like:
     *
     *    - get-or-create (your function)
     *    - lookup (e.g., "do we already have chunk X?")
     *    - erase / eviction (remove chunk from map)
     *    - iteration over bucket entries (evict scan, TTL scan, stats, debugging)
     *    - rehash-affecting operations (emplace, erase, potentially reserve/rehash)
     *
     * So, the bucket mutex is really: "permission to mutate or safely inspect the map."
     * Once you have the ChunkRecord pointer, you're responsible for synchronization
     * E.g:
     * - std::once_flag/call_once around stage publication
     * - stage state machine / atomics
     * - stage futures
     * - RecordPin to keep the record from being evicted out from under tasks/users
    */

    struct ChunkRecord; // forward declare is fine here
    // HOWEVER - Since ChunkRecords are not trivially destructible,
	// we require the full definition of ChunkRecord in order to destroy them when we evict them from the map.
	// Hence, our .cpp implementation of StripeBucket defines the destructor out-of-line, where we can include 
    // the full definition of ChunkRecord without blowing anything to smithereens.

    // This is currently our Terrain Chunk storage during generation stages.
    // This data should be able to be safely discarded once the final renderable is derived.
    struct StripeBucket {
        std::mutex mut;
        std::unordered_map<ChunkCoord, std::unique_ptr<ChunkRecord>, ChunkCoordHash> map;

        StripeBucket();
        ~StripeBucket();                // <- key: out-of-line
        StripeBucket(const StripeBucket&) = delete;
        StripeBucket& operator=(const StripeBucket&) = delete;
    };

} // namespace aveng