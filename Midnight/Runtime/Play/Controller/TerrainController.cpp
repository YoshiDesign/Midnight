#include <cstdio>
#include <memory>
#include <mutex>
#include "TerrainController.h"
#include "Module/Procgen/Terrain/ChunkManager.h"
#include "CoreVK/EngineDevice.h"
#include "Utils/Logger.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "Module/Procgen/Rendering/VkTerrain.h"



namespace aveng {
	
	TerrainController::TerrainController(EngineDevice& engineDevice, VkRenderData& renderData, ChunkManager& chunks) noexcept 
        : chunks_(&chunks), engineDevice_(engineDevice), renderData_{renderData}
	{
        // Clearly, the Controller must outlive the Chunk Manager
		std::printf("[%s] Constructing TerrainController\n", __FUNCTION__);
        chunks_->initManagers(&erosionMgr_);
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
        ensureChunkRequested(start_coord);
    }

    void TerrainController::evictChunks(ChunkCoord center) {
    
    }

    void TerrainController::update(/*const Camera& camera*/)
    {
        // auto desired = computeDesiredChunkSet(camera);

        // requestMissingChunks(desired);
        drainCompletedTerrain();
        serviceCpuReadyChunks();
        // evictUndesiredChunks(desired);
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
        int uploadsThisFrame = 0;
        for (auto& [coord, slot] : slots_) {

            // Rate limit
            if (uploadsThisFrame >= kMaxUploadsPerFrame) { break; }

            // Ignore
            if (slot.state != procgen::TerrainRuntimeState::CpuReady) {
                continue;
            }

            // Upload and transfer state
            if (uploadTerrainChunkToGpu(engineDevice_, renderData_, slot, settingsUboBuffer_, settingsUboSize_)) {
                slot.gpu.valid = true;
                slot.cpuRenderable.reset();
            }
            else {
                slot.state = procgen::TerrainRuntimeState::Failed;
            }

            uploadsThisFrame++;

        }
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
        auto& slot = slots_[coord]; // insert-or-lookup behavior, default construction
        slot.coord = coord;

        if (slot.state == procgen::TerrainRuntimeState::Requested ||
            slot.state == procgen::TerrainRuntimeState::CpuReady ||
            slot.state == procgen::TerrainRuntimeState::Uploading ||
            slot.state == procgen::TerrainRuntimeState::Resident)
        {
            return;
        }

        slot.requestId = chunks_->requestRenderableAsync(coord, frameIndex_);
        slot.state = procgen::TerrainRuntimeState::Requested;
    }

    void TerrainController::drainCompletedTerrain()
    {
        chunks_->drainCompletedRenderables([this](const procgen::RenderableCompletion& completedChunk) {

            auto it = slots_.find(completedChunk.coord);
            if (it == slots_.end()) {
                return; // no longer desired
            }

            auto& slot = it->second;

            // Ignore stale completions
            if (slot.requestId != completedChunk.requestId) {
                return;
            }

            if (!completedChunk.success) {
                slot.state = procgen::TerrainRuntimeState::Failed;
                return;
            }

            std::unique_ptr<procgen::TerrainRenderable> cpuRenderable;
            /**
             *   Note: the streaming architecture doesn't need the ChunkRecord to cache its renderable at all
             *   in some cases which would make the indirection to tryTakeRenderable unnecessary.
             *   We could simply deliver the renderable into the RenderableCompletion directly.
             *   The queue's ownership transfers would become larger, however.
             */
            if (!chunks_->tryTakeRenderable(completedChunk.coord, completedChunk.requestId, cpuRenderable)) {
                std::printf("A potentially stale renderable was requested!\n");
                return;
            }

            slot.cpuRenderable = std::move(cpuRenderable);
            slot.state = procgen::TerrainRuntimeState::CpuReady;
        });
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

    // Cleanup all
    void TerrainController::cleanup(){
        for (auto& [coord, slot] : slots_) {
            cleanupOne(slot);
        }

        vkDestroyDescriptorSetLayout(engineDevice_.device(), renderData_.rdTerrainLitGraphicsDescriptorSetLayout, nullptr);

        vkDestroyDescriptorPool(engineDevice_.device(), renderData_.avengBasicTerrainDescriptorPool, nullptr);
        vkDestroyDescriptorPool(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool, nullptr);
        vkDestroyDescriptorPool(engineDevice_.device(), renderData_.avengTerrainComputeDescriptorPool, nullptr);
    }

    void TerrainController::cleanupOne(procgen::TerrainChunkSlot& slot) {
        VertexBuffer::cleanup(engineDevice_, slot.gpu.draw.vertexBuffer);
        IndexBuffer::cleanup(engineDevice_, slot.gpu.draw.indexBuffer);

        if (slot.gpu.packed.graphicsDescriptorSet != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(engineDevice_.device(), renderData_.avengTerrainLitDescriptorPool,
                1, &slot.gpu.packed.graphicsDescriptorSet);
            slot.gpu.packed.graphicsDescriptorSet = VK_NULL_HANDLE;
        }

        if (slot.gpu.packed.inputSsbo.buffer != VK_NULL_HANDLE) {
            ShaderStorageBuffer::cleanup(engineDevice_, slot.gpu.packed.inputSsbo);
        }
        if (slot.gpu.packed.outputSsbo.buffer != VK_NULL_HANDLE) {
            ShaderStorageBuffer::cleanup(engineDevice_, slot.gpu.packed.outputSsbo);
        }
    }

}