---
name: ""
overview: ""
todos: []
isProject: false
---

# Fix drainCompletedTerrain Stall and Verify Swapchain Image Fix

## Context (what we've proven so far)

Three rounds of diagnosis have narrowed the frame stutter to two root causes:

### Root cause 1: Swapchain image starvation (FIXED, needs verification)

With MAILBOX presentation, 3 swapchain images, and `MAX_FRAMES_IN_FLIGHT = 3`, the presentation engine holds 2 images (displayed + pending), leaving only 1 for rendering. Consecutive frames acquire the same image and the `rdImagesInFlight` fence serializes them -- destroying triple-buffering. This caused 20-73ms stalls even at idle.

**Fix applied**: [swapchain.cpp](Midnight/CoreVK/swapchain.cpp) line 120 now requests `MAX_FRAMES_IN_FLIGHT + 1` (4) images. Console should now print `Swapchain Image count: 4`. Verify the idle image fence stalls are gone.

### Root cause 2: `drainCompletedTerrain` blocks the render thread for 4-54ms

This is the remaining stutter source during terrain streaming. Phase-level timing proved it:

```
[TerrainController::tick] 53.94 ms (flush: 0.00, drain: 53.86, service: 0.08)
```

`drain` dominates. `flush` and `service` are negligible.

## Analysis of the drain path

`drainCompletedTerrain` ([TerrainController.cpp](Midnight/Runtime/Play/Controller/TerrainController.cpp) ~459-494) calls `ConcurrentQueue::drain` ([ConcurrentQueue.h](Midnight/Runtime/Threading/ConcurrentQueue.h) ~26-37) which:

1. Locks mutex, swaps the queue into `scratch_` (O(1))
2. Unlocks mutex
3. Iterates `scratch_`, calling the lambda for each `RenderableCompletion`
4. Calls `scratch_.clear()` -- destructs all `RenderableCompletion` items

The lambda per item does: unordered_map lookup, pointer moves, and possibly `recycleRenderable`. Each of these operations should cost microseconds. Yet the total is 4-54ms.

### Why drain is slow: two hypotheses

**Hypothesis A -- batch size**: Worker threads complete many chunks between frames. If 10+ chunks complete in a burst (common when moving into a new area), all 10 are drained in one `scratch` loop. Even if each item costs 0.5ms (map lookup + recycle + potential deallocation), 10 items = 5ms, 20 items = 10ms.

**Hypothesis B -- heap deallocation on the render thread**: `recycleRenderable` ([TerrainController.cpp](Midnight/Runtime/Play/Controller/TerrainController.cpp) ~576-585) clears vectors and pushes the shell to `pool_.renderables`. But:

- If the pool is full (`pool_.renderables.size() >= kMaxPooledRenderables` which is 32), the `unique_ptr<TerrainRenderable>` is NOT pushed -- it falls off the end of the function and the destructor fires, deallocating 5 vectors worth ~2MB total.
- Even when recycled, `vector::clear()` keeps capacity but the TerrainRenderable is a ~2MB object. If many renderables are recycled simultaneously, the heap work could be significant.
- `scratch_.clear()` at the end of `ConcurrentQueue::drain` also destructs any remaining `RenderableCompletion` objects, though the renderable unique_ptr should be null by then.

The "throbbing" stutter pattern (ebb and flow every ~1 second) matches burst-drain behavior: chunks complete in waves, drain processes the entire wave in one frame, that frame stutters, then smooth rendering until the next wave.

## Changes

### Step 1: Verify swapchain image fix

Run the application idle (no terrain streaming). Check:

- Console prints `Swapchain Image count: 4`
- Image fence stall messages should be gone or rare
- If stalls persist, the image count fix didn't help and needs further investigation

### Step 2: Instrument drain batch size and cost

**File**: [TerrainController.cpp](Midnight/Runtime/Play/Controller/TerrainController.cpp) -- inside `drainCompletedTerrain`

Add a counter to track how many items the drain processes per call, and log it alongside the drain time when it exceeds 2ms. This confirms/denies Hypothesis A:

```cpp
int drainCount = 0;
int recycledCount = 0;
int assignedCount = 0;

chunks_->drainCompletedRenderables([&](procgen::RenderableCompletion& completedChunk) {
    drainCount++;
    // ... existing lambda body, but increment recycledCount/assignedCount
    // in the appropriate branches
});

if (drainTime > 2.0f) {
    std::printf("[TerrainController] drain: %.2f ms, items: %d (assigned: %d, recycled: %d)\n",
        drainTime, drainCount, assignedCount, recycledCount);
    fflush(stdout);
}
```

If `drainCount` is large (10+) when stalls occur, the fix is to rate-limit the drain (Step 3).
If `drainCount` is small (1-3) but time is still high, the issue is per-item cost or OS preemption.

### Step 3: Rate-limit drain to N items per frame

**File**: [ConcurrentQueue.h](Midnight/Runtime/Threading/ConcurrentQueue.h) -- add a `drainN` method:

```cpp
template <typename Fn>
int drainN(Fn&& fn, int maxItems)
{
    {
        std::scoped_lock lock(mutex_);
        scratch_.swap(queue_);
    }

    int processed = 0;
    for (auto& item : scratch_) {
        fn(item);
        if (++processed >= maxItems) break;
    }

    // Return unprocessed items to the front of the queue
    if (processed < static_cast<int>(scratch_.size())) {
        std::scoped_lock lock(mutex_);
        queue_.insert(queue_.begin(),
            std::make_move_iterator(scratch_.begin() + processed),
            std::make_move_iterator(scratch_.end()));
    }
    scratch_.clear();
    return processed;
}
```

**File**: [ChunkManager.h](Midnight/Module/Procgen/Terrain/ChunkManager.h) -- add a rate-limited drain method:

```cpp
template <typename Fn>
int drainCompletedRenderablesN(Fn&& fn, int maxItems)
{
    return completedRenderables_.drainN(std::forward<Fn>(fn), maxItems);
}
```

**File**: [TerrainController.cpp](Midnight/Runtime/Play/Controller/TerrainController.cpp) -- in `drainCompletedTerrain`, switch from `drainCompletedRenderables` to `drainCompletedRenderablesN` with a cap (start with 3, tune based on results):

```cpp
chunks_->drainCompletedRenderablesN([this](procgen::RenderableCompletion& completedChunk) {
    // ... same lambda ...
}, kMaxDrainPerFrame);
```

Add `static constexpr int kMaxDrainPerFrame = 3;` to [TerrainController.h](Midnight/Runtime/Play/Controller/TerrainController.h).

This spreads the drain cost across multiple frames instead of processing everything in one burst. The remaining items stay in the queue and get processed next frame.

### Step 4 (optional): Investigate per-item drain cost

If Step 2 shows small batch sizes with high times, the per-item cost is the problem. Instrument inside the drain lambda to time the map lookup, recycleRenderable, and identify whether heap deallocation (pool full) is the culprit. If `pool_.renderables.size() >= kMaxPooledRenderables` frequently, consider increasing `kMaxPooledRenderables` or switching to a deferred-free strategy.

## Key files reference

| File                                                                                     | Role                                                             |
| ---------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| [swapchain.cpp](Midnight/CoreVK/swapchain.cpp) ~118-121                                  | Swapchain image count (already fixed)                            |
| [TerrainController.cpp](Midnight/Runtime/Play/Controller/TerrainController.cpp) ~459-494 | `drainCompletedTerrain` -- the stall site                        |
| [TerrainController.cpp](Midnight/Runtime/Play/Controller/TerrainController.cpp) ~576-585 | `recycleRenderable` -- potential deallocation                    |
| [ConcurrentQueue.h](Midnight/Runtime/Threading/ConcurrentQueue.h)                        | `drain` method -- processes all items unbounded                  |
| [TerrainResourcePool.h](Midnight/Module/Procgen/Rendering/TerrainResourcePool.h)         | Pool cap: `kMaxPooledRenderables = 32`                           |
| [BasicTerrainAsset.h](Midnight/Module/Procgen/Rendering/BasicTerrainAsset.h) ~56-71      | `TerrainRenderable` struct -- 5 vectors, ~2MB total              |
| [ChunkManager.h](Midnight/Module/Procgen/Terrain/ChunkManager.h) ~132-135                | `drainCompletedRenderables` template                             |
| [Renderer.cpp](Midnight/Core/Renderer/Renderer.cpp) ~357-455                             | `beginFrame` -- fence wait, acquire, image fence instrumentation |
| [AvengFrame.cpp](Midnight/Core/Renderer/AvengFrame.cpp) ~143-150                         | `tickTerrain` wall-time timer                                    |

## Expected outcomes

| Result                                                       | Interpretation                                                                                    |
| ------------------------------------------------------------ | ------------------------------------------------------------------------------------------------- |
| Idle image fence stalls gone after 4-image fix               | Swapchain starvation confirmed and resolved                                                       |
| Drain count is 10+ when stalls occur                         | Burst completion is the cause; rate-limiting (Step 3) will fix it                                 |
| Drain count is 1-3 but time is 20ms+                         | Per-item cost or OS preemption; investigate Step 4                                                |
| Rate-limited drain eliminates throbbing                      | Problem solved -- terrain streams in smoothly over multiple frames                                |
| Rate-limited drain causes backlog (cpuReady grows unbounded) | Drain cap too low or chunks arriving faster than upload can consume; tune cap or add backpressure |
