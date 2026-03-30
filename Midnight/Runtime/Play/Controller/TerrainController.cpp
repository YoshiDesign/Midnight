#include <cassert>
#include <cstdio>
#include <memory>
#include "TerrainController.h"
#include "Module/Procgen/Terrain/ChunkManager.h"
#include "CoreVK/EngineDevice.h"
#include "Utils/Logger.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

/**
* Note on Mesh Assembly and recycled resources:
* Calls during mesh assembly reuse that capacity without heap allocation 
* (terrain chunks produce similarly-sized data). If no recycled renderable is available (cold start), 
* a fresh one is allocated.
* After building, the completed renderable is pushed onto the ConcurrentQueue<RenderableCompletion> with a success flag.
* 
* Some helpful terminology:
* - Retired: Acknowledging that a GPU upload has finished and
* transitioning the chunk from Uploading to Resident. It's the moment 
* the CPU confirms the GPU is done with the transfer and the chunk is safe to render
* 
* - In flight: The GPU is processing the data it needs, initializing buffers, etc.
*/


namespace aveng {
	
	TerrainController::TerrainController(EngineDevice& engineDevice, VkRenderData& renderData, ChunkManager& chunks) noexcept 
        : chunks_(&chunks), engineDevice_(engineDevice), renderData_{renderData}
	{
		std::printf("[%s] Constructing TerrainController\n", __FUNCTION__);
        chunks_->initManagers(&erosionMgr_);
        chunks_->setAdmissionController(&admission_, kSupportRadius);

        for (size_t i = 0; i < uploadBatches_.size(); ++i) {
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCreateFence(engineDevice_.device(), &fenceInfo, nullptr, &uploadBatches_[i].fence);

            VkCommandBufferAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocInfo.commandPool = engineDevice_.commandPoolGraphics();
            allocInfo.commandBufferCount = 1;
            vkAllocateCommandBuffers(engineDevice_.device(), &allocInfo, &uploadBatches_[i].cmdBuffer);
        }
	}

    TerrainController::~TerrainController() {
        cleanup();
    }

    // TODO - this isn't the only mgr which will need a frame index. Put it in 1 place and read it every frame.
    void TerrainController::setFrameIndex(uint64_t frameIndex) noexcept {
        frameIndex_ = frameIndex;
    }

    // Direct access to generate a chunk for debugging/editor
    void TerrainController::generateChunks(ChunkCoord start_coord) {
        //Logger::log(1, "Start {%d, %d}\tFrameIndex", start_coord.x, start_coord.z, frameIndex_);
        ensureChunkRequested(start_coord);
    }

    // @Step 7 - When the streamer determines a chunk is out of range, it evicts.
    // The slot's GPU resource handles (VBO, IBO, input SSBO, output SSBO, descriptor sets) are copied into a DeferredGpuCleanup.
    // The slot is erased immediately, but the VK resources are not destroyed yet. Not just due to the recycle strat,
    // but because the GPU could still be using them.
    void TerrainController::evictChunk(ChunkCoord center) {
        auto it = slots_.find(center);
        if (it == slots_.end()) { return; }

        auto& slot = it->second;

        if (slot.state == procgen::TerrainRuntimeState::Uploading) {
            for (const auto& batch : uploadBatches_) {
                if (!batch.active) continue;
                for (const auto& c : batch.inFlightSlots) {
                    if (c.x == center.x && c.z == center.z) {
                        throw std::runtime_error("Attempting to evict a chunk which is currently uploading.");
                    }
                }
            }
        }

        admission_.release(center, kSupportRadius);

        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                chunks_->evictRecord({ center.x + dx, center.z + dz });
            }
        }

        // TODO - Another declaration for a local tmp...
        DeferredGpuCleanup deferred{};
        deferred.vertexBuffer = slot.gpu.draw.vertexBuffer;
        deferred.indexBuffer = slot.gpu.draw.indexBuffer;
        deferred.inputSsbo = slot.gpu.packed.inputSsbo;
        deferred.outputSsbo = slot.gpu.packed.outputSsbo;
        deferred.graphicsDescriptorSet = slot.gpu.packed.graphicsDescriptorSet;
        deferred.computeDescriptorSet = slot.gpu.packed.computeDescriptorSet;
        deferred.retireFrame = frameIndex_;
        deferredCleanups_.push_back(deferred);

        slots_.erase(center);
    }

    void TerrainController::tick()
    {
        tickPhaseTimer_.start();
        flushDeferredDeletes();
        float flushTime = tickPhaseTimer_.stop();

        tickPhaseTimer_.start();
        drainCompletedTerrain();
        float drainTime = tickPhaseTimer_.stop();

        tickPhaseTimer_.start();
        serviceCpuReadyChunks();
        float serviceTime = tickPhaseTimer_.stop();

        float total = flushTime + drainTime + serviceTime;
        if (total > 2.0f) {
            std::printf("[TerrainController::tick] %.2f ms (flush: %.2f, drain: %.2f, service: %.2f)\n",
                total, flushTime, drainTime, serviceTime);
            fflush(stdout);
        }
    }

    void TerrainController::update(/*const Camera& camera*/)
    {
        tick();
    }

    // @Step 6: Render the completed work for all Resident chunks
    void TerrainController::render(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex)
    {

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        for (auto const& [coord, slot] : slots_) {
            if (slot.state != procgen::TerrainRuntimeState::Resident) {
                continue;
            }

            if (!slot.gpu.valid || slot.gpu.draw.indexCount == 0) {
                continue;
            }

            if (slot.gpu.packed.graphicsDescriptorSet == VK_NULL_HANDLE) {
                continue;
            }

            // Bind per-chunk graphics descriptor set at set 1
            VkDescriptorSet chunkSets[] = { slot.gpu.packed.graphicsDescriptorSet };
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                renderData_.rdTerrainLitPipelineLayout, 1, 1, chunkSets, 0, nullptr);

            glm::mat4 modelMat{ 1.0 };
            dpc.modelMat = modelMat;

            vkCmdPushConstants(
                cmd,
                renderData_.rdTerrainLitPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(VkBasicTerrainDebugPC),
                &dpc
            );

            VkBuffer vb = slot.gpu.draw.vertexBuffer.buffer;
            VkDeviceSize offsets[] = { 0 };

            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, offsets);
            vkCmdBindIndexBuffer(cmd, slot.gpu.draw.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(cmd, slot.gpu.draw.indexCount, 1, 0, 0, 0);
        }
    }
    
    // RenderVK
    void TerrainController::renderDebug(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex)
    {

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData_.rdAvengEditorBasicTerrainPipeline);

        VkDescriptorSet renderSets[1] = { renderData_.rdAvengBasicTerrainDescriptorSets[currentFrameIndex] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData_.rdAvengBasicTerrainPipelineLayout, 
                                0, 1, renderSets, 0, nullptr);

        for (auto const& [coord, slot] : slots_) {
            if (slot.state != procgen::TerrainRuntimeState::Resident) {
                continue;
            }

            if (!slot.gpu.valid || slot.gpu.draw.indexCount == 0) {
                continue;
            }

            glm::mat4 modelMat{ 1.0 };

            dpc.modelMat = modelMat;

            vkCmdPushConstants(
                cmd,
                renderData_.rdAvengBasicTerrainPipelineLayout, // or your unified layout, whichever created basicPipeline
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(VkBasicTerrainDebugPC),
                &dpc
            );

            // fn(coord, slot);

            VkBuffer vb = slot.gpu.draw.vertexBuffer.buffer;
            VkDeviceSize offsets[] = { 0 };

            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, offsets);
            vkCmdBindIndexBuffer(cmd, slot.gpu.draw.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(cmd, slot.gpu.draw.indexCount, 1, 0, 0, 0);
        }
    }

    void TerrainController::serviceCpuReadyChunks()
    {
        retireCompletedUploads();
        buildAndSubmitUploadBatch();
    }

    int TerrainController::countActiveUploads() const
    {
        int count = 0;
        for (const auto& batch : uploadBatches_) {
            if (batch.active) ++count;
        }
        return count;
    }

    int TerrainController::countCpuReadySlots() const
    {
        int count = 0;
        for (const auto& [coord, slot] : slots_) {
            if (slot.state == procgen::TerrainRuntimeState::CpuReady) ++count;
        }
        return count;
    }

    /**
    * @Step 5:
    * `retireCompletedUploads` (called every frame) does 3 things:
    * 
    * 1. Checks the fence -- 
    * vkGetFenceStatus polls whether the GPU has finished executing the upload command buffer. 
    * If the fence hasn't signaled, the function returns immediately
    * 
    * 2. Transitions slot state -- 
    * Each chunk in the batch goes from Uploading to Resident, and gpu.valid is set to true. 
    * From this point forward, the render loop will draw this chunk.
    *
    * 3.Releases the CPU renderable -- 
    * The cpuRenderable (the ~2MB of vertex/index/adjacency vectors that were memcpy'd into 
    * staging buffers during upload prep) is no longer needed. It's recycled via 
    * `recycleRenderable` (vectors cleared, shell pushed to pool)
    */
    void TerrainController::retireCompletedUploads()
    {
#ifdef M_DEBUG
        retireTimer.start();
#endif
        for (auto& batch : uploadBatches_) {
            if (!batch.active) { continue; }

            if (vkGetFenceStatus(engineDevice_.device(), batch.fence) != VK_SUCCESS) {
                continue;
            }

            for (const ChunkCoord& coord : batch.inFlightSlots) {
                auto it = slots_.find(coord);
                if (it == slots_.end()) { continue; }

                auto& slot = it->second;
                slot.gpu.valid = true;
                slot.state = procgen::TerrainRuntimeState::Resident;

                recycleRenderable(std::move(slot.cpuRenderable));
            }

            batch.inFlightSlots.clear();
            batch.active = false;
        }

#ifdef M_DEBUG
        renderData_.rdTerrainRetireTime = retireTimer.stop();
        if (renderData_.rdTerrainRetireTime > renderData_.rdTerrainRetireTimeMAX) {
            renderData_.rdTerrainRetireTimeMAX = renderData_.rdTerrainRetireTime;
        }
#endif

    }

    /**
    * @Step 4: GPU Upload
    * When a buffer is acquired from the pool, its Vulkan handles are reused wholesale -- 
    * device buffer, staging buffer, VMA allocation all still valid. fillStaging maps the 
    * existing staging buffer and overwrites its contents. recordCopy copies staging to device. 
    * Zero vmaCreateBuffer/vmaDestroyBuffer on the hot path.
    * 
    * The entire batch is submitted with a single vkQueueSubmit behind a fence. Slot state transitions to Uploading
    */
    void TerrainController::buildAndSubmitUploadBatch()
    {
        const int batchIdx = static_cast<int>(frameIndex_ % renderData_.MAX_FRAMES_IN_FLIGHT);
        auto& batch = uploadBatches_[batchIdx];

        if (batch.active) { return; }

#ifdef M_DEBUG
        if (batch.fence != VK_NULL_HANDLE) {
            VkResult fenceStatus = vkGetFenceStatus(engineDevice_.device(), batch.fence);
            if (fenceStatus == VK_NOT_READY) {
                std::printf("[TerrainController] BUG: uploadBatch[%d].cmdBuffer reuse attempted while fence is still unsignaled!\n", batchIdx);
                assert(false && "Upload command buffer reuse before fence signaled");
            }
        }
#endif

        engineDevice_.beginSingleShotCommand(batch.cmdBuffer);

        int chunksThisBatch = 0;

        for (auto& [coord, slot] : slots_) {
            if (chunksThisBatch >= kMaxChunksPerUploadBatch) { break; }
            if (slot.state != procgen::TerrainRuntimeState::CpuReady) { continue; }
#ifdef M_DEBUG
            vkBufferInitTimer.start();
#endif
            if (!prepareChunkUpload(engineDevice_, renderData_, slot, settingsUboBuffer_, settingsUboSize_, pool_)) {
                slot.state = procgen::TerrainRuntimeState::Failed;
                continue;
            }
#ifdef M_DEBUG
            renderData_.rdTerrainBufferTimeInit = vkBufferInitTimer.stop();
            if (renderData_.rdTerrainBufferTimeInit > renderData_.rdTerrainBufferTimeInitMAX) {
                renderData_.rdTerrainBufferTimeInitMAX = renderData_.rdTerrainBufferTimeInit;
            }
            vkCopyBufferTimer.start();
#endif

            recordChunkCopies(batch.cmdBuffer, slot);
#ifdef M_DEBUG
            renderData_.rdTerrainCopyBufferTime = vkCopyBufferTimer.stop();
            if (renderData_.rdTerrainCopyBufferTime > renderData_.rdTerrainCopyBufferTimeMAX) {
                renderData_.rdTerrainCopyBufferTimeMAX = renderData_.rdTerrainCopyBufferTime;
            }
#endif
            slot.state = procgen::TerrainRuntimeState::Uploading;
            batch.inFlightSlots.push_back(coord);
            chunksThisBatch++;
        }

        if (batch.inFlightSlots.empty()) {
            vkEndCommandBuffer(batch.cmdBuffer);
            return;
        }

        vkResetFences(engineDevice_.device(), 1, &batch.fence);

        /* The TerrainController owns the upload batch and the command buffer, so it's responsible for submitting it. */
        /* This also ensures that the GPU is busy before the renderPasses begin, and not just idle. */
        submitTerrainQueue(engineDevice_, batch
#ifdef M_DEBUG
			, renderData_
#endif
        );
    }

    void TerrainController::setTerrainConfig(TerrainConfig tcfg)
    {
        chunks_->setGlobalConfig(tcfg);   
    }

    void TerrainController::setTerrainNoiseParams(noise::NoiseParams noise)
    {
        chunks_->setNoiseParams(noise);
    }

    void TerrainController::setTerrainWeatheringParams(ErosionSettings erosion)
    {
        chunks_->setErosionParameters(erosion);
    }

    float TerrainController::getChunkSize() {
        return chunks_->chunkSize();
    }

    //bool TerrainController::

    //std::unique_ptr<procgen::TerrainRenderable> const& TerrainController::lastRequestedRenderable() const noexcept {
    //    return lastRequested_;
    //}

    ChunkCoord TerrainController::offsetCoord(ChunkCoord base, int dx, int dz) noexcept {
        base.x += dx;
        base.z += dz;
        return base;
    }

    /*
    * @Step 1 `ensureChunkRequested`: The TerrainController creates (or finds) a slot in slots_ for the requested coordinate. 
    * It first checks the admission controller to ensure this chunk's 5x5 support footprint doesn't 
    * overlap any chunk currently being built (our concurrency guardian)
    */
    void TerrainController::ensureChunkRequested(ChunkCoord coord)
    {
        auto& slot = slots_[coord];
        slot.coord = coord;

        if (slot.state == procgen::TerrainRuntimeState::Requested ||
            slot.state == procgen::TerrainRuntimeState::CpuReady  ||
            slot.state == procgen::TerrainRuntimeState::Uploading ||
            slot.state == procgen::TerrainRuntimeState::Resident)
        {
            return;
        }

        // First Concurrency Guard: Queue an active request - do not allow overlapping chunks to generate simultaneously
        // Gate: only admit if the 5x5 support footprint doesn't overlap an active build.
        if (!admission_.tryAcquire(coord, kSupportRadius)) {
            // Unable to push onto admission_.active, so we push to the deferred stack
            // which will be retried after 
            deferredRequests_.push_back(coord);
            return;
        }

        std::unique_ptr<procgen::TerrainRenderable> recycled;
        if (!pool_.renderables.empty()) {
            recycled = std::move(pool_.renderables.back());
            pool_.renderables.pop_back();
        }

        /** 
         * @Step 2 -  Async Generation - 
         * The recycled renderable is stored on the center ChunkRecord under `renderableMutex` 
         * The renderable lives on the record and survives any number of retry-enqueue cycles in `runGenerate`.
         * `runGenerate` runs on a worker thread. It resolves all 25 ChunkRecord pointers for the 5x5 neighborhood, 
         * pins them (preventing eviction), kicks off all sub-stages and polls for readiness.
         * If dependencies aren't satisfied, it re-enqueues itself via CAS-guarded retry.
         */
        slot.requestId = chunks_->requestRenderableAsync(coord, frameIndex_, std::move(recycled));
        slot.state = procgen::TerrainRuntimeState::Requested;
    }

    /* called every frame */
    void TerrainController::drainCompletedTerrain()
    {
#ifdef M_DEBUG
        drainTimer.start();
#endif
        /** 
         * @Step 3: Collect completed renderables. If the slot is gone (evicted between build and drain) 
         * or the requestId is stale (superseded by a newer request), the renderable is recycled back to the 
         * pool instead of being silently destroyed. When the local deque destructs at the end of drain, all 
         * unique_ptr members are null -- zero debug-heap work.
         * After draining, admission is released and any deferred requests (from @Step 1) are retried.
         */
        chunks_->drainCompletedRenderables([this](procgen::RenderableCompletion& completedChunk) {

            auto renderable = std::move(completedChunk.renderable);

            auto it = slots_.find(completedChunk.coord);
            if (it == slots_.end()) {
                recycleRenderable(std::move(renderable));
                return;
            }

            auto& slot = it->second;
            if (slot.requestId != completedChunk.requestId) {
                recycleRenderable(std::move(renderable));
                return;
            }

            if (!completedChunk.success || !renderable) {
                slot.state = procgen::TerrainRuntimeState::Failed;
                return;
            }

            slot.cpuRenderable = std::move(renderable);
            slot.state = procgen::TerrainRuntimeState::CpuReady;
        });

#ifdef M_DEBUG
        renderData_.rdTerrainDrainTime = drainTimer.stop();
        if (renderData_.rdTerrainDrainTime > renderData_.rdTerrainDrainTimeMAX) {
            renderData_.rdTerrainDrainTimeMAX = renderData_.rdTerrainDrainTime;
        }
#endif

		// Retry any requests that were deferred due to region overlap - See: First Concurrency guard in ensureChunkRequested()
        if (!deferredRequests_.empty()) {
			// TODO - No need to initialize a vector here every time - Just keep it around and clear it.
            std::vector<ChunkCoord> pending;
            pending.swap(deferredRequests_);
            for (const ChunkCoord& coord : pending) {
                ensureChunkRequested(coord);
            }
        }
    }

    // Check 1 chunk - Not very useful here, honestly
    bool TerrainController::hasAllPointsReady(const ChunkCoord coord) noexcept {
        return chunks_->isAllPointsReady(coord);
    }

    // Check that an entire 5x5 region has completed up to the spatial grid
    bool TerrainController::hasRegionReady(ChunkCoord center) noexcept {
        return chunks_->isRegionAllPointsReady(center);
    }

    // Check that an entire 5x5 region has completed up to the spatial grid and its inner 3x3 has fully completed
    bool TerrainController::hasRegionComplete(ChunkCoord center) noexcept {
        return chunks_->isRegionAllPointsReady(center)
            && chunks_->isRegionAllStagesComplete(center);
    }

    /** 
    * @Step 8
    * `flushDeferredDeletes` (called every frame) handles the gap between when the CPU decides a chunk should be evicted 
    * and when it's actually safe to reclaim that chunk's GPU resources. Waits for `kDeferFrames` to pass based
    * on the `retireFrame` of cleanup struct. Descriptor sets are freed but buffers return to the pool.
    * These pooled buffers will be reused in @Step 4 the next time prepareChunkUpload runs.
    */
    void TerrainController::flushDeferredDeletes()
    {

#ifdef M_DEBUG
        vkCleanupDeferredDeletesTimer.start();
#endif

        size_t i = 0;
        while (i < deferredCleanups_.size()) {
            if (frameIndex_ - deferredCleanups_[i].retireFrame >= kDeferFrames) {
                auto& entry = deferredCleanups_[i];

                pool_.vbo.push_back(std::move(entry.vertexBuffer));
                pool_.ibo.push_back(std::move(entry.indexBuffer));
                if (entry.inputSsbo.buffer != VK_NULL_HANDLE)
                    pool_.inputSsbo.push_back(std::move(entry.inputSsbo));
                if (entry.outputSsbo.buffer != VK_NULL_HANDLE)
                    pool_.outputSsbo.push_back(std::move(entry.outputSsbo));

                if (entry.graphicsDescriptorSet != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, 1, &entry.graphicsDescriptorSet);
                if (entry.computeDescriptorSet != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, 1, &entry.computeDescriptorSet);

                // O(1) swap-and-pop removal
                deferredCleanups_[i] = std::move(deferredCleanups_.back());
                deferredCleanups_.pop_back();
            } else {
                ++i;
            }
        }
#ifdef M_DEBUG
        renderData_.rdTerrainCleanupDeferredDeletesTime = vkCleanupDeferredDeletesTimer.stop();
        if (renderData_.rdTerrainCleanupDeferredDeletesTime > renderData_.rdTerrainCleanupDeferredDeletesTimeMAX) {
            renderData_.rdTerrainCleanupDeferredDeletesTimeMAX = renderData_.rdTerrainCleanupDeferredDeletesTime;
        }
#endif
    }

    void TerrainController::recycleRenderable(std::unique_ptr<procgen::TerrainRenderable> r) {
        if (!r) return;
        r->vbo.clear();
        r->ibo.clear();
        r->packedPositions.clear();
        r->packedTriangles.clear();
        r->packedAdjacency.clear();
        if (pool_.renderables.size() < TerrainResourcePool::kMaxPooledRenderables)
            pool_.renderables.push_back(std::move(r));
    }

    // Cleanup all -- Funnels all resources into pool objects and then destroys from there.
    void TerrainController::cleanup(){

        // 1. Wait for all in-flight upload batches
        for (auto& batch : uploadBatches_) {
            if (batch.active) {
                vkWaitForFences(engineDevice_.device(), 1, &batch.fence, VK_TRUE, UINT64_MAX);
            }
        }
        retireCompletedUploads();

        // 2. Destroy fences and command buffers
        for (auto& batch : uploadBatches_) {
            if (batch.fence != VK_NULL_HANDLE)
                vkDestroyFence(engineDevice_.device(), batch.fence, nullptr);
            if (batch.cmdBuffer != VK_NULL_HANDLE)
                vkFreeCommandBuffers(engineDevice_.device(), engineDevice_.commandPoolGraphics(), 1, &batch.cmdBuffer);
        }

        // 3. Recycle all slot resources into pool (free descriptor sets immediately)
        for (auto& [coord, slot] : slots_) {
            pool_.vbo.push_back(std::move(slot.gpu.draw.vertexBuffer));
            pool_.ibo.push_back(std::move(slot.gpu.draw.indexBuffer));
            if (slot.gpu.packed.inputSsbo.buffer != VK_NULL_HANDLE)
                pool_.inputSsbo.push_back(std::move(slot.gpu.packed.inputSsbo));
            if (slot.gpu.packed.outputSsbo.buffer != VK_NULL_HANDLE)
                pool_.outputSsbo.push_back(std::move(slot.gpu.packed.outputSsbo));
            if (slot.gpu.packed.graphicsDescriptorSet != VK_NULL_HANDLE)
                vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, 1, &slot.gpu.packed.graphicsDescriptorSet);
            if (slot.gpu.packed.computeDescriptorSet != VK_NULL_HANDLE)
                vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, 1, &slot.gpu.packed.computeDescriptorSet);
        }

        // 4. Recycle all deferred cleanup resources into pool (free descriptor sets immediately)
        for (auto& d : deferredCleanups_) {
            pool_.vbo.push_back(std::move(d.vertexBuffer));
            pool_.ibo.push_back(std::move(d.indexBuffer));
            if (d.inputSsbo.buffer != VK_NULL_HANDLE)
                pool_.inputSsbo.push_back(std::move(d.inputSsbo));
            if (d.outputSsbo.buffer != VK_NULL_HANDLE)
                pool_.outputSsbo.push_back(std::move(d.outputSsbo));
            if (d.graphicsDescriptorSet != VK_NULL_HANDLE)
                vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, 1, &d.graphicsDescriptorSet);
            if (d.computeDescriptorSet != VK_NULL_HANDLE)
                vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, 1, &d.computeDescriptorSet);
        }
        deferredCleanups_.clear();

        // 5. Drain pool -- actually destroy all Vulkan buffers
        pool_.destroyAll(engineDevice_);

        // 6. Destroy descriptor pools (must come AFTER pool drain)
        vkDestroyDescriptorSetLayout(engineDevice_.device(), renderData_.rdTerrainLitGraphicsDescriptorSetLayout, nullptr);

        vkDestroyDescriptorPool(engineDevice_.device(), renderData_.avengBasicTerrainDescriptorPool, nullptr);
        vkDestroyDescriptorPool(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, nullptr);
        vkDestroyDescriptorPool(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, nullptr);
    }

    void TerrainController::cleanupOne(procgen::TerrainChunkSlot& slot) {

        VertexBuffer::cleanup(engineDevice_, slot.gpu.draw.vertexBuffer);
        IndexBuffer::cleanup(engineDevice_, slot.gpu.draw.indexBuffer);

		// These descriptor set cleanups are technically optional since the sets 
        // will be implicitly freed when the pool is destroyed, 
        // but it's good practice/acceptable to clean up explicitly
        if (slot.gpu.packed.graphicsDescriptorSet != VK_NULL_HANDLE) {

            vkFreeDescriptorSets(
                engineDevice_.device(),
                renderData_.avengTerrainLitDescriptorPool,
                1, 
                &slot.gpu.packed.graphicsDescriptorSet);

            slot.gpu.packed.graphicsDescriptorSet = VK_NULL_HANDLE;
        }

        if (slot.gpu.packed.computeDescriptorSet != VK_NULL_HANDLE) {

            vkFreeDescriptorSets(
                engineDevice_.device(), 
                renderData_.avengTerrainComputeDescriptorPool,
                1, 
                &slot.gpu.packed.computeDescriptorSet);

            slot.gpu.packed.computeDescriptorSet = VK_NULL_HANDLE;
        }

        if (slot.gpu.packed.inputSsbo.buffer != VK_NULL_HANDLE) {
            ShaderStorageBuffer::destroy(engineDevice_, slot.gpu.packed.inputSsbo);
        }
        if (slot.gpu.packed.outputSsbo.buffer != VK_NULL_HANDLE) {
            ShaderStorageBuffer::destroy(engineDevice_, slot.gpu.packed.outputSsbo);
        }

    }

}