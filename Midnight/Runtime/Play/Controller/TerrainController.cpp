#include <cstdio>
#include <memory>
#include "TerrainController.h"
#include "Module/Procgen/Terrain/ChunkManager.h"
#include "CoreVK/EngineDevice.h"
#include "Utils/Logger.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "Module/Procgen/Rendering/VkTerrain.h"
#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>



namespace aveng {
	
	TerrainController::TerrainController(EngineDevice& engineDevice, VkRenderData& renderData, ChunkManager& chunks) noexcept 
        : chunks_(&chunks), engineDevice_(engineDevice), renderData_{renderData}
	{
		std::printf("[%s] Constructing TerrainController\n", __FUNCTION__);
        chunks_->initManagers(&erosionMgr_);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = 0;
        vkCreateFence(engineDevice_.device(), &fenceInfo, nullptr, &uploadBatch_.fence);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = engineDevice_.commandPoolGraphics();
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(engineDevice_.device(), &allocInfo, &uploadBatch_.cmdBuffer);
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
        Logger::log(1, "Start {%d, %d}\tFrameIndex", start_coord.x, start_coord.z, frameIndex_);
        ensureChunkRequested(start_coord);
    }

    void TerrainController::evictChunk(ChunkCoord center) {
        auto it = slots_.find(center);
        if (it == slots_.end()) return;

        auto& slot = it->second;

        if (slot.state == procgen::TerrainRuntimeState::Uploading && uploadBatch_.active) {
            vkWaitForFences(engineDevice_.device(), 1, &uploadBatch_.fence, VK_TRUE, UINT64_MAX);
            retireCompletedUploads();
        }

        admission_.release(center, kSupportRadius);

        for (int dz = -2; dz <= 2; ++dz) {
            for (int dx = -2; dx <= 2; ++dx) {
                chunks_->evictRecord({ center.x + dx, center.z + dz });
            }
        }

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

    void TerrainController::update(/*const Camera& camera*/)
    {
        flushDeferredDeletes();
        drainCompletedTerrain();
        serviceCpuReadyChunks();
    }

    // Render VK
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

    void TerrainController::retireCompletedUploads()
    {
        if (!uploadBatch_.active) return;

        if (vkGetFenceStatus(engineDevice_.device(), uploadBatch_.fence) != VK_SUCCESS) return;

        for (const ChunkCoord& coord : uploadBatch_.inFlightSlots) {
            auto it = slots_.find(coord);
            if (it == slots_.end()) continue;

            auto& slot = it->second;
            slot.gpu.valid = true;
            slot.cpuRenderable.reset();
            slot.state = procgen::TerrainRuntimeState::Resident;

            VertexBuffer::cleanupStaging(engineDevice_, slot.gpu.draw.vertexBuffer);
            IndexBuffer::cleanupStaging(engineDevice_, slot.gpu.draw.indexBuffer);
        }

        uploadBatch_.inFlightSlots.clear();
        vkResetFences(engineDevice_.device(), 1, &uploadBatch_.fence);
        uploadBatch_.active = false;
    }

    void TerrainController::buildAndSubmitUploadBatch()
    {
        if (uploadBatch_.active) return;

        engineDevice_.beginSingleShotCommand(uploadBatch_.cmdBuffer);

        int chunksThisBatch = 0;

        for (auto& [coord, slot] : slots_) {
            if (chunksThisBatch >= kMaxChunksPerUploadBatch) break;
            if (slot.state != procgen::TerrainRuntimeState::CpuReady) continue;

            if (!prepareChunkUpload(engineDevice_, renderData_, slot, settingsUboBuffer_, settingsUboSize_)) {
                slot.state = procgen::TerrainRuntimeState::Failed;
                continue;
            }

            recordChunkCopies(uploadBatch_.cmdBuffer, slot);

            slot.state = procgen::TerrainRuntimeState::Uploading;
            uploadBatch_.inFlightSlots.push_back(coord);
            chunksThisBatch++;
        }

        if (uploadBatch_.inFlightSlots.empty()) {
            vkEndCommandBuffer(uploadBatch_.cmdBuffer);
            return;
        }

        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;

        vkCmdPipelineBarrier(
            uploadBatch_.cmdBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr
        );

        vkEndCommandBuffer(uploadBatch_.cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &uploadBatch_.cmdBuffer;

        vkQueueSubmit(engineDevice_.graphicsQueue(), 1, &submitInfo, uploadBatch_.fence);
        uploadBatch_.active = true;
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

    void TerrainController::ensureChunkRequested(ChunkCoord coord)
    {
        auto& slot = slots_[coord];
        slot.coord = coord;

        if (slot.state == procgen::TerrainRuntimeState::Requested ||
            slot.state == procgen::TerrainRuntimeState::CpuReady ||
            slot.state == procgen::TerrainRuntimeState::Uploading ||
            slot.state == procgen::TerrainRuntimeState::Resident)
        {
            return;
        }

        // Gate: only admit if the 5x5 support footprint doesn't overlap an active build.
        if (!admission_.tryAcquire(coord, kSupportRadius)) {
            deferredRequests_.push_back(coord);
            return;
        }

        slot.requestId = chunks_->requestRenderableAsync(coord, frameIndex_);
        slot.state = procgen::TerrainRuntimeState::Requested;
    }

    void TerrainController::drainCompletedTerrain()
    {
        chunks_->drainCompletedRenderables([this](const procgen::RenderableCompletion& completedChunk) {

            // Release the admission slot so deferred requests can proceed
            admission_.release(completedChunk.coord, kSupportRadius);

            auto it = slots_.find(completedChunk.coord);
            if (it == slots_.end())
                return;

            auto& slot = it->second;

            if (slot.requestId != completedChunk.requestId)
                return;

            if (!completedChunk.success) {
                slot.state = procgen::TerrainRuntimeState::Failed;
                return;
            }

            std::unique_ptr<procgen::TerrainRenderable> cpuRenderable;
            if (!chunks_->tryTakeRenderable(completedChunk.coord, completedChunk.requestId, cpuRenderable)) {
                std::printf("A potentially stale renderable was requested!\n");
                return;
            }

            slot.cpuRenderable = std::move(cpuRenderable);
            slot.state = procgen::TerrainRuntimeState::CpuReady;
        });

        // Retry any requests that were deferred due to region overlap
        if (!deferredRequests_.empty()) {
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

    // For higher perf, no cache lookups on renderables - Direct access on TerrainRenderable
    //void TerrainController::drainCompletedTerrainALT()
    //{
    //    chunks_->drainCompletedRenderables([this](RenderableCompletion& done) {
    //        auto it = slots_.find(done.coord);
    //        if (it == slots_.end()) return;

    //        auto& slot = it->second;
    //        if (slot.requestId != done.requestId) return;

    //        if (!done.success || !done.renderable) {
    //            slot.state = TerrainRuntimeState::Failed;
    //            return;
    //        }

    //        slot.cpuRenderable = std::move(done.renderable);
    //        slot.state = TerrainRuntimeState::CpuReady;
    //    });
    //}

    void TerrainController::flushDeferredDeletes()
    {
        auto it = deferredCleanups_.begin();
        while (it != deferredCleanups_.end()) {
            if (frameIndex_ - it->retireFrame >= kDeferFrames) {
                VertexBuffer::cleanup(engineDevice_, it->vertexBuffer);
                IndexBuffer::cleanup(engineDevice_, it->indexBuffer);
                if (it->inputSsbo.buffer != VK_NULL_HANDLE)
                    vmaDestroyBuffer(engineDevice_.allocator(), it->inputSsbo.buffer, it->inputSsbo.bufferAlloc);
                if (it->outputSsbo.buffer != VK_NULL_HANDLE)
                    vmaDestroyBuffer(engineDevice_.allocator(), it->outputSsbo.buffer, it->outputSsbo.bufferAlloc);
                if (it->graphicsDescriptorSet != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, 1, &it->graphicsDescriptorSet);
                if (it->computeDescriptorSet != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, 1, &it->computeDescriptorSet);
                it = deferredCleanups_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Cleanup all
    void TerrainController::cleanup(){

        if (uploadBatch_.active) {
            vkWaitForFences(engineDevice_.device(), 1, &uploadBatch_.fence, VK_TRUE, UINT64_MAX);
            retireCompletedUploads();
        }

        if (uploadBatch_.fence != VK_NULL_HANDLE)
            vkDestroyFence(engineDevice_.device(), uploadBatch_.fence, nullptr);
        if (uploadBatch_.cmdBuffer != VK_NULL_HANDLE)
            vkFreeCommandBuffers(engineDevice_.device(), engineDevice_.commandPoolGraphics(), 1, &uploadBatch_.cmdBuffer);

        for (auto& [coord, slot] : slots_) {
            cleanupOne(slot);
        }

        for (auto& d : deferredCleanups_) {
            VertexBuffer::cleanup(engineDevice_, d.vertexBuffer);
            IndexBuffer::cleanup(engineDevice_, d.indexBuffer);
            if (d.inputSsbo.buffer != VK_NULL_HANDLE)
                vmaDestroyBuffer(engineDevice_.allocator(), d.inputSsbo.buffer, d.inputSsbo.bufferAlloc);
            if (d.outputSsbo.buffer != VK_NULL_HANDLE)
                vmaDestroyBuffer(engineDevice_.allocator(), d.outputSsbo.buffer, d.outputSsbo.bufferAlloc);
        }
        deferredCleanups_.clear();

        vkDestroyDescriptorSetLayout(engineDevice_.device(), renderData_.rdTerrainLitGraphicsDescriptorSetLayout, nullptr);

        vkDestroyDescriptorPool(engineDevice_.device(), renderData_.avengBasicTerrainDescriptorPool, nullptr);
        vkDestroyDescriptorPool(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, nullptr);
        vkDestroyDescriptorPool(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, nullptr);
    }

    void TerrainController::cleanupOne(procgen::TerrainChunkSlot& slot) {
        VertexBuffer::cleanup(engineDevice_, slot.gpu.draw.vertexBuffer);
        IndexBuffer::cleanup(engineDevice_, slot.gpu.draw.indexBuffer);

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
            ShaderStorageBuffer::cleanup(engineDevice_, slot.gpu.packed.inputSsbo);
        }
        if (slot.gpu.packed.outputSsbo.buffer != VK_NULL_HANDLE) {
            ShaderStorageBuffer::cleanup(engineDevice_, slot.gpu.packed.outputSsbo);
        }
    }

}