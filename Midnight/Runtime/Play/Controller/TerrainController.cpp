#include <cassert>
#include <cmath>
#include <cstdio>
#include "TerrainController.h"
#include "Module/Procgen/Terrain/ChunkManager2.h"
#include "CoreVK/EngineDevice.h"
#include "Utils/Logger.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

/**
* Note on Mesh Assembly:
* Renderables live inline in the slots_ vector at stable addresses. Workers write directly
* into the slot's renderable via raw pointer. Completion is signaled via a lightweight
* CompletionNotice (slot index + requestId + success). Vector capacities are preserved
* across reuse, so repeated generation is allocation-free after warmup.
* 
* Some helpful terminology:
* - Retired: Acknowledging that a GPU upload has finished and
* transitioning the chunk from Uploading to Resident. It's the moment 
* the CPU confirms the GPU is done with the transfer and the chunk is safe to render
* 
* - In flight: The GPU is processing the data it needs, initializing buffers, etc.
*/


namespace aveng {
	
	TerrainController::TerrainController(EngineDevice& engineDevice, VkRenderData& renderData, procgen::ChunkManager2& chunks) noexcept 
        : chunks_(&chunks), engineDevice_(engineDevice), renderData_{renderData}
	{
		std::printf("[%s] Constructing TerrainController\n", __FUNCTION__);
	}

    TerrainController::~TerrainController() {
        cleanup();
    }

    void TerrainController::init() {

        std::printf("[%s] Initializing TerrainController\n", __FUNCTION__);
        chunks_->initManagers(&erosionMgr_);
        chunks_->setAdmissionController(&admission_, kSupportRadius);
        chunks_->setTerrainPool(&terrain_pool); // This needs to occur before midnight calls chunkManager_.init()

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

    void TerrainController::tick()
    {
        //tickPhaseTimer_.start();
        flushDeferredDeletes();
        //float flushTime = tickPhaseTimer_.stop();

        //tickPhaseTimer_.start();
        drainCompletedTerrain();
        //float drainTime = tickPhaseTimer_.stop();

        //tickPhaseTimer_.start();
        serviceCpuReadyChunks();
        //float serviceTime = tickPhaseTimer_.stop();

        //float total = flushTime + drainTime + serviceTime;
        //if (total > 2.0f) {
        //    std::printf("[TerrainController::tick] %.2f ms (flush: %.2f, drain: %.2f, service: %.2f)\n",
        //        total, flushTime, drainTime, serviceTime);
        //    fflush(stdout);
        //}
    }

    void TerrainController::setTerrainConfig(procgen::TerrainConfig tcfg)
    {
        chunks_->setGlobalConfig(tcfg);
    }

    void TerrainController::setTerrainNoiseParams(noise::NoiseParams noise)
    {
        chunks_->setNoiseParams(noise);
    }

    void TerrainController::setTerrainWeatheringParams(procgen::ErosionSettings erosion)
    {
        chunks_->setErosionParameters(erosion);
    }

    float TerrainController::getChunkSize() {
        return chunks_->chunkSize();
    }

    // This is currently only used by our rudimentary chunk culling
    // The terrain streamer also does this, maybe merge the two (TODO)
    void TerrainController::setDrawCenter(glm::vec3 worldPos) noexcept {
        currentCenter_.x = static_cast<int32_t>(std::floor(worldPos.x / chunks_->chunkSize()));
        currentCenter_.z = static_cast<int32_t>(std::floor(worldPos.z / chunks_->chunkSize()));
    }

    // TODO - this isn't the only mgr which will need a frame index. Put it in 1 place and read it every frame.
    void TerrainController::setFrameIndex(uint64_t frameIndex) noexcept {
        frameIndex_ = frameIndex;
    }

    // Direct access to generate a chunk for debugging/editor
    void TerrainController::generateChunks(procgen::ChunkCoord coord) {

        if (!admission_.allow()) {
            // Requested, but the admission controller deems we're not ready for another request.
            // This happens due to two requests including overlapping regions.
            // Deferred requests are still considered "active" regions within the admission controller.
            deferredRequests_.push_back(coord);
            return;
        }

        uint32_t idx = allocateSlot(coord);
        auto& slot = slots_[idx];

        //std::printf("[TerrainController] Creating renderable at slot[%d] addr=%p "
        //            "(slots size=%zu cap=%zu)\n",
        //            idx, (void*)&slot.renderable, slots_.size(), slots_.capacity());

        /**
         * @Step 2 - Async Generation
         * A raw pointer to the slot's inline renderable is passed to the ChunkManager.
         * The worker thread writes directly into this renderable. Completion is signaled
         * via a lightweight CompletionNotice containing the slot index.
         */
         //#ifdef M_DEBUG
         //        assert(slot.coord == coord && "slot coord mismatch\n");
         //#endif
        slot.requestId = chunks_->requestRenderableAsync(coord, frameIndex_, &slot.renderable, idx);
        slot.state = procgen::TerrainRuntimeState::Requested;
    }

    uint32_t TerrainController::allocateSlot(procgen::ChunkCoord coord) {

        procgen::ChunkHandle handle;

        uint32_t idx;
        if (!freeSlots_.empty()) {
            // Free slots are LIFO
            idx = freeSlots_.back();
            freeSlots_.pop_back();
        }
        else {
            idx = static_cast<uint32_t>(slots_.size());
//#ifdef M_DEBUG
//            if (slots_.size() == slots_.capacity()) {
//                for (const auto& s : slots_) {
//                    if (s.state == procgen::TerrainRuntimeState::Requested) {
//                        std::printf("[TerrainController] FATAL: slots_ reallocation while slot {%d,%d} "
//                                    "is in Requested state -- worker pointers will dangle! "
//                                    "(size=%zu, capacity=%zu)\n",
//                                    s.coord.x, s.coord.z, slots_.size(), slots_.capacity());
//                        assert(false && "slots_ reallocation would invalidate in-flight renderable pointers");
//                    }
//                }
//                std::printf("[TerrainController] WARNING: slots_ growing beyond reserve "
//                            "(size=%zu, capacity=%zu)\n", slots_.size(), slots_.capacity());
//            }
//#endif
            slots_.emplace_back(); // init
        }

        // Some setup
        slots_[idx].coord = coord;
        slots_[idx].state = procgen::TerrainRuntimeState::Unrequested;

        // Map coord to index
        coordToSlot_[coord] = idx;
        return idx;
    }

    void TerrainController::releaseSlot(uint32_t idx) {
        auto& slot = slots_[idx];
//#ifdef M_DEBUG
//        assert(slot.state != procgen::TerrainRuntimeState::Requested &&
//            "Cannot release a slot while a worker thread holds a pointer to its renderable");
//#endif
        coordToSlot_.erase(slot.coord);
        slot.renderable.resetKeepCapacity();
        slot.state = procgen::TerrainRuntimeState::Unrequested;
        slot.coord = {};
        slot.requestId = 0;
        slot.gpu = {};

        freeSlots_.push_back(idx);
    }

    // @Step 7 - When the streamer determines a chunk is out of range, it evicts.
    // The slot's GPU resource handles (VBO, IBO, input SSBO, output SSBO, descriptor sets) are copied into a DeferredGpuCleanup.
    // The slot is released to the free-list, but the VK resources are not destroyed yet, in case they're still in use
    void TerrainController::evictChunk(ChunkCoord center) {
        // evictionTimer.start();
        auto it = coordToSlot_.find(center);
        if (it == coordToSlot_.end()) { return; }

        uint32_t idx = it->second;
        auto& slot = slots_[idx];

        //if (slot.state == procgen::TerrainRuntimeState::Requested) {
        //    std::printf("[TerrainController] WARNING: evicting slot[%d] {%d,%d} while still Requested!\n",
        //                idx, slot.coord.x, slot.coord.z);
        //}

//#ifdef M_DEBUG
//        assert(slot.state != procgen::TerrainRuntimeState::Requested &&
//               "Cannot evict a slot while a worker thread holds a pointer to its renderable");
//
//        if (slot.state == procgen::TerrainRuntimeState::Uploading) {
//            for (const auto& batch : uploadBatches_) {
//                if (!batch.active) continue;
//                for (uint32_t batchIdx : batch.inFlightSlots) {
//                    if (batchIdx == idx) {
//                        throw std::runtime_error("Attempting to evict a chunk which is currently uploading.");
//                    }
//                }
//            }
//        }
//#endif

        admission_.release(center, kSupportRadius);

//// #region agent log
//        auto _dbg_ev_t0 = std::chrono::steady_clock::now();
//// #endregion
//
//        int _evSuccess = chunks_->batchEvictRegion(center);
//
//// #region agent log
//        auto _dbg_ev_t1 = std::chrono::steady_clock::now();
//// #endregion

        // Move the retiree's vulkan resources into a deffered cleanup struct
        // TODO: Maybe just push the resources back onto the pool. This strategy predates the reusable pool resources
        DeferredGpuCleanup deferred{};
        deferred.vertexBuffer = slot.gpu.draw.vertexBuffer;
        deferred.indexBuffer = slot.gpu.draw.indexBuffer;
        deferred.inputSsbo = slot.gpu.packed.inputSsbo;
        deferred.outputSsbo = slot.gpu.packed.outputSsbo;
        deferred.graphicsDescriptorSet = slot.gpu.packed.graphicsDescriptorSet;
        deferred.computeDescriptorSet = slot.gpu.packed.computeDescriptorSet;
        deferred.retireFrame = frameIndex_;
        deferredCleanups_.push_back(deferred);

        releaseSlot(idx);

//// #region agent log
//        { auto _dbg_ev_t2 = std::chrono::steady_clock::now();
//          float _evLoopMs = std::chrono::duration<float,std::milli>(_dbg_ev_t1-_dbg_ev_t0).count();
//          float _evRestMs = std::chrono::duration<float,std::milli>(_dbg_ev_t2-_dbg_ev_t1).count();
//          float _evTotalMs = std::chrono::duration<float,std::milli>(_dbg_ev_t2-_dbg_ev_t0).count();
//          FILE* _f;
//          fopen_s(&_f, "c:/Users/Yoshi/dev/Midnight/debug-ed8025.log", "a");
//          if(_f){ std::fprintf(_f,"{\"sessionId\":\"ed8025\",\"runId\":\"batch-fix\",\"hypothesisId\":\"B\",\"location\":\"TerrainController.cpp:evictChunk\",\"message\":\"eviction summary\",\"data\":{\"evictRecordLoopMs\":%.3f,\"deferredPushAndReleaseMs\":%.3f,\"totalMs\":%.3f,\"coord\":[%d,%d],\"evictSuccess\":%d},\"timestamp\":%lld}\n",_evLoopMs,_evRestMs,_evTotalMs,center.x,center.z,_evSuccess,(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); std::fclose(_f);} }
//// #endregion

#ifdef M_DEBUG
        //renderData_.rdTerrainEvictionTime = evictionTimer.stop();
        //if (renderData_.rdTerrainEvictionTime > renderData_.rdTerrainEvictionTimeMAX) {
        //    renderData_.rdTerrainEvictionTimeMAX = renderData_.rdTerrainEvictionTime;
        //}
#endif
    }

    // @Step 6: Render the completed work for all Resident chunks
    void TerrainController::render(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex)
    {

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        for (int i = 0; i < procgen::MAX_CHUNK_RECORDS; i++) {
            if (slots_[i].state != procgen::TerrainRuntimeState::Resident) {
                continue;
            }

            if (!slots_[i].gpu.valid || slots_[i].gpu.draw.indexCount == 0) {
                continue;
            }

            // Rudimentary culling - This refers to chunk-space coordinates
            int dx = slots_[i].record.coord.x - currentCenter_.x;
            int dz = slots_[i].record.coord.z - currentCenter_.z;
            if (dx > kDrawRadius || dx < -kDrawRadius ||
                dz > kDrawRadius || dz < -kDrawRadius) {
                continue;
            }

            if (slots_[i].gpu.packed.graphicsDescriptorSet == VK_NULL_HANDLE) {
                continue;
            }

            VkDescriptorSet chunkSets[] = { slots_[i].gpu.packed.graphicsDescriptorSet };
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

            VkBuffer vb = slots_[i].gpu.draw.vertexBuffer.buffer;
            VkDeviceSize offsets[] = { 0 };

            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, offsets);
            vkCmdBindIndexBuffer(cmd, slots_[i].gpu.draw.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(cmd, slots_[i].gpu.draw.indexCount, 1, 0, 0, 0);
        }
    }
    
    void TerrainController::renderDebug(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex)
    {

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData_.rdAvengEditorBasicTerrainPipeline);

        VkDescriptorSet renderSets[1] = { renderData_.rdAvengBasicTerrainDescriptorSets[currentFrameIndex] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData_.rdAvengBasicTerrainPipelineLayout, 
                                0, 1, renderSets, 0, nullptr);

        for (int i = 0; i < procgen::MAX_CHUNK_RECORDS; i++) {
            if (slots_[i].state != procgen::TerrainRuntimeState::Resident) {
                continue;
            }

            if (!slots_[i].gpu.valid || slots_[i].gpu.draw.indexCount == 0) {
                continue;
            }

            // Rudimentary culling - This refers to chunk-space coordinates
            int dx = slots_[i].record.coord.x - currentCenter_.x;
            int dz = slots_[i].record.coord.z - currentCenter_.z;
            if (dx > kDrawRadius || dx < -kDrawRadius ||
                dz > kDrawRadius || dz < -kDrawRadius) {
                continue;
            }

            glm::mat4 modelMat{ 1.0 };

            dpc.modelMat = modelMat;

            vkCmdPushConstants(
                cmd,
                renderData_.rdAvengBasicTerrainPipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(VkBasicTerrainDebugPC),
                &dpc
            );

            VkBuffer vb = slots_[i].gpu.draw.vertexBuffer.buffer;
            VkDeviceSize offsets[] = { 0 };

            vkCmdBindVertexBuffers(cmd, 0, 1, &vb, offsets);
            vkCmdBindIndexBuffer(cmd, slots_[i].gpu.draw.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

            vkCmdDrawIndexed(cmd, slots_[i].gpu.draw.indexCount, 1, 0, 0, 0);
        }
    }

    void TerrainController::serviceCpuReadyChunks()
    {
        retireCompletedUploads();
        buildAndSubmitUploadBatch();
    }

    /*int TerrainController::countActiveUploads() const
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
        for (const auto& slot : slots_) {
            if (slot.state == procgen::TerrainRuntimeState::CpuReady) ++count;
        }
        return count;
    }

    int TerrainController::countResidentSlots() const
    {
        int count = 0;
        for (const auto& slot : slots_) {
            if (slot.state == procgen::TerrainRuntimeState::Resident) ++count;
        }
        return count;
    }*/

    /**
    * @Step 5:
    * `retireCompletedUploads` (called every frame) does 3 things:
    * 
    * 1. Checks the fence -- 
    * vkGetFenceStatus polls whether the GPU has finished executing the upload command buffer. 
    * If the fence hasn't signaled, the iteration continues
    * 
    * 2. Transitions slot state -- 
    * Each chunk in the batch goes from Uploading to Resident, and gpu.valid is set to true. 
    * From this point forward, the render loop will draw this chunk.
    *
    * 3. Releases CPU renderable data -- 
    * The renderable vectors (~2MB of vertex/index/adjacency data that were memcpy'd into 
    * staging buffers during upload prep) are no longer needed. resetKeepCapacity() clears
    * them while preserving allocation capacity for the next use of this slot.
    */
    void TerrainController::retireCompletedUploads()
    {
//#ifdef M_DEBUG
//        retireTimer.start();
//#endif
        for (auto& batch : uploadBatches_) {
            if (!batch.active) { continue; }

            if (vkGetFenceStatus(engineDevice_.device(), batch.fence) != VK_SUCCESS) {
                continue;
            }

            for (uint32_t idx : batch.inFlightSlots) {
                auto& slot = slots_[idx];
                slot.gpu.valid = true;
                slot.state = procgen::TerrainRuntimeState::Resident;
                slot.renderable.resetKeepCapacity();
            }

            batch.inFlightSlots.clear();
            batch.active = false;
        }
//
//#ifdef M_DEBUG
//        renderData_.rdTerrainRetireTime = retireTimer.stop();
//        if (renderData_.rdTerrainRetireTime > renderData_.rdTerrainRetireTimeMAX) {
//            renderData_.rdTerrainRetireTimeMAX = renderData_.rdTerrainRetireTime;
//        }
//#endif

    }

    /**
    * @Step 4: GPU Upload
    * When a buffer is acquired from the pool, its Vulkan handles are reused -- 
    * device buffer, staging buffer, VMA allocation all still valid. fillStaging maps the 
    * existing staging buffer and overwrites its contents. recordCopy copies staging to device. 
    * No calls to vmaCreateBuffer/vmaDestroyBuffer on the hot path.
    * 
    * The entire batch is submitted with a single vkQueueSubmit behind a fence. Slot state transitions to Uploading
    */
    void TerrainController::buildAndSubmitUploadBatch()
    {
        // Buffering
        // TODO - get rid of this modulo!!!
        const int batchIdx = static_cast<int>(frameIndex_ % renderData_.MAX_FRAMES_IN_FLIGHT);
        auto& batch = uploadBatches_[batchIdx];

        if (batch.active) { return; }

        engineDevice_.beginSingleShotCommand(batch.cmdBuffer);

        int chunksThisBatch = 0;

        for (uint32_t i = 0; i < terrain_pool.nActive; ++i) {
            if (chunksThisBatch >= kMaxChunksPerUploadBatch) { break; }
            auto& slot = slots_[i];
            if (slot.state != procgen::TerrainRuntimeState::CpuReady) { continue; }

            if (!prepareChunkUpload(engineDevice_, renderData_, slot, settingsUboBuffer_, settingsUboSize_, pool_)) {
                slot.state = procgen::TerrainRuntimeState::Failed;
                continue;
            }

            recordChunkCopies(batch.cmdBuffer, slot);

            slot.state = procgen::TerrainRuntimeState::Uploading;
            batch.inFlightSlots.push_back(i);
            chunksThisBatch++;
        }

        if (batch.inFlightSlots.empty()) {
            // Nothing happened
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

    /* DEPRECATED */
    /* called every frame */
    void TerrainController::drainCompletedTerrain()
    {

        /** 
         * @Step 3: The lambda itself is a simple O(1), but the concurrent queue processes in O(n).
         * No ownership transfers -- the renderable was written in-place by the worker.
         * After draining, any deferred requests (from @Step 1) are retried.
         * `notice` represents an item from the queue
         */
        chunks_->drainCompletedRenderables([this](const procgen::CompletionNotice& notice) {
            auto& slot = slots_[notice.slotIndex];

            // This slot is now targeted for rendering, or it failed
            slot.state = notice.success
                ? procgen::TerrainRuntimeState::CpuReady
                : procgen::TerrainRuntimeState::Failed;
        });

//#ifdef M_DEBUG
//        renderData_.rdTerrainDrainTime = drainTimer.stop();
//        if (renderData_.rdTerrainDrainTime > renderData_.rdTerrainDrainTimeMAX) {
//            renderData_.rdTerrainDrainTimeMAX = renderData_.rdTerrainDrainTime;
//        }
//#endif

        if (!deferredRequests_.empty()) {
            std::vector<ChunkCoord> pending;
            pending.swap(deferredRequests_);
            for (const ChunkCoord& coord : pending) {
                generateChunks(coord);
            }
        }
    }

    /* DEPRECATED */
    // Check 1 chunk - Not very useful here, honestly
    bool TerrainController::hasSpatialGridReady(const procgen::ChunkCoord coord) noexcept {
        return chunks_->isSpatialGridReady(coord);
    }

    /* DEPRECATED */
    // Check that an entire 5x5 region has completed up to the spatial grid
    bool TerrainController::hasRegionReady(procgen::ChunkCoord center) noexcept {
        return chunks_->isRegionSpatialGridReady(center);
    }

    /* DEPRECATED */
    // Check that an entire 5x5 region has completed up to the spatial grid and its inner 3x3 has fully completed
    bool TerrainController::hasRegionComplete(procgen::ChunkCoord center) noexcept {
        return chunks_->isRegionSpatialGridReady(center)
            && chunks_->isRegionAllStagesComplete(center);
    }

    /* SUPER DEPRECATED */
    /** 
    * @Step 8
    * `flushDeferredDeletes` (called every frame) handles the gap between when the CPU decides a chunk should be evicted 
    * and when it's actually safe to reclaim that chunk's GPU resources. Waits for `kDeferFrames` to pass based
    * on the `retireFrame` of cleanup struct. Descriptor sets are freed but buffers return to the pool.
    * These pooled buffers will be reused in @Step 4 the next time prepareChunkUpload runs.
    */
    void TerrainController::flushDeferredDeletes()
    {

//#ifdef M_DEBUG
//        vkCleanupDeferredDeletesTimer.start();
//#endif

        // TODO: This may actually be wiser to perform when we retire slots
        size_t i = 0;
        while (i < deferredCleanups_.size()) {
            if (frameIndex_ - deferredCleanups_[i].retireFrame >= kDeferFrames) {
                auto& entry = deferredCleanups_[i];

                pool_.vbo.push_back(std::move(entry.vertexBuffer));
                pool_.ibo.push_back(std::move(entry.indexBuffer));

                if (entry.inputSsbo.buffer != VK_NULL_HANDLE) {
                    pool_.inputSsbo.push_back(std::move(entry.inputSsbo));
                }

                if (entry.outputSsbo.buffer != VK_NULL_HANDLE) {
                    pool_.outputSsbo.push_back(std::move(entry.outputSsbo));
                }

                if (entry.graphicsDescriptorSet != VK_NULL_HANDLE) {
                    vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, 1, &entry.graphicsDescriptorSet);
                }

                if (entry.computeDescriptorSet != VK_NULL_HANDLE) {
                    vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, 1, &entry.computeDescriptorSet);
                }

                // O(1) swap-and-pop removal
                deferredCleanups_[i] = std::move(deferredCleanups_.back());
                deferredCleanups_.pop_back();
            } else {
                ++i;
            }
        }

        while (pool_.vbo.size() > TerrainResourcePool::kMaxPooledBuffers) {
            VertexBuffer::cleanup(engineDevice_, pool_.vbo.back());
            pool_.vbo.pop_back();
        }
        while (pool_.ibo.size() > TerrainResourcePool::kMaxPooledBuffers) {
            IndexBuffer::cleanup(engineDevice_, pool_.ibo.back());
            pool_.ibo.pop_back();
        }
        while (pool_.inputSsbo.size() > TerrainResourcePool::kMaxPooledBuffers) {
            ShaderStorageBuffer::destroy(engineDevice_, pool_.inputSsbo.back());
            pool_.inputSsbo.pop_back();
        }
        while (pool_.outputSsbo.size() > TerrainResourcePool::kMaxPooledBuffers) {
            ShaderStorageBuffer::destroy(engineDevice_, pool_.outputSsbo.back());
            pool_.outputSsbo.pop_back();
        }

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
            if (batch.fence != VK_NULL_HANDLE) {
                vkDestroyFence(engineDevice_.device(), batch.fence, nullptr);
            }

            if (batch.cmdBuffer != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(engineDevice_.device(), engineDevice_.commandPoolGraphics(), 1, &batch.cmdBuffer);
            }
        }

        // 3. Recycle all slot resources into pool (free descriptor sets immediately)
        for (int i = 0; i < procgen::MAX_CHUNK_RECORDS; i++) {
            if (slots_[i].state == procgen::TerrainRuntimeState::Unrequested) continue;

            pool_.vbo.push_back(std::move(slots_[i].gpu.draw.vertexBuffer));
            pool_.ibo.push_back(std::move(slots_[i].gpu.draw.indexBuffer));

            if (slots_[i].gpu.packed.inputSsbo.buffer != VK_NULL_HANDLE) {
                pool_.inputSsbo.push_back(std::move(slots_[i].gpu.packed.inputSsbo));
            }

            if (slots_[i].gpu.packed.outputSsbo.buffer != VK_NULL_HANDLE) {
                pool_.outputSsbo.push_back(std::move(slots_[i].gpu.packed.outputSsbo));
            }

            if (slots_[i].gpu.packed.graphicsDescriptorSet != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, 1, &slots_[i].gpu.packed.graphicsDescriptorSet);
            }
            
            if (slots_[i].gpu.packed.computeDescriptorSet != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, 1, &slots_[i].gpu.packed.computeDescriptorSet);
            }
        }

        // 4. Recycle all deferred cleanup resources into pool (free descriptor sets immediately)
        for (auto& d : deferredCleanups_) {
            pool_.vbo.push_back(std::move(d.vertexBuffer));
            pool_.ibo.push_back(std::move(d.indexBuffer));

            if (d.inputSsbo.buffer != VK_NULL_HANDLE) {
                pool_.inputSsbo.push_back(std::move(d.inputSsbo));
            }

            if (d.outputSsbo.buffer != VK_NULL_HANDLE) {
                pool_.outputSsbo.push_back(std::move(d.outputSsbo));
            }

            if (d.graphicsDescriptorSet != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, 1, &d.graphicsDescriptorSet);
            }

            if (d.computeDescriptorSet != VK_NULL_HANDLE) {
                vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, 1, &d.computeDescriptorSet);
            }
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

    void TerrainController::cleanupOne(procgen::ChunkSlot& slot) {

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