#include <cstdio>
#include <memory>
#include <mutex>
#include "TerrainController.h"
//#include "Module/Procgen/Rendering/BasicTerrainAsset.h"
#include "Module/Procgen/Terrain/ChunkManager.h"
#include "CoreVK/EngineDevice.h"
#include "Utils/Logger.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/VertexBuffer.h"


namespace aveng {
	
	TerrainController::TerrainController(EngineDevice& engineDevice, VkRenderData& renderData, ChunkManager& chunks) noexcept 
        : chunks_(&chunks), engineDevice_(engineDevice), renderData_{renderData}
	{
        // Clearly, the Controller must outlive the Chunk Manager
		std::printf("[%s] Constructing TerrainController\n", __FUNCTION__);
        chunks_->initManagers(&erosionMgr_);
	}

    // TODO - this isn't the only mgr which will need a frame index. Put it in 1 place and read it every frame.
    void TerrainController::setFrameIndex(uint64_t frameIndex) noexcept {
        frameIndex_ = frameIndex;
    }

    // Direct access to generate a chunk for debugging/editor
    void TerrainController::generateChunks(ChunkCoord start_coord, int cols, int rows) {

        std::printf("Generating Chunks...\nStart: {%d, %d} \n", start_coord.x, start_coord.z);

        ensureChunkRequested(start_coord);
        return;

    }

    void TerrainController::update(/*const Camera& camera*/)
    {
        // auto desired = computeDesiredChunkSet(camera);

        // requestMissingChunks(desired);
        drainCompletedTerrain();
        serviceCpuReadyChunks();
        // evictUndesiredChunks(desired);
    }

    void TerrainController::render(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex)
    {
        for (auto const& [coord, slot] : slots_) {
            if (slot.state != TerrainRuntimeState::Resident) {
                continue;
            }

            if (!slot.gpu.valid || slot.gpu.draw.indexCount == 0) {
                continue;
            }

            // Bind pipeline
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

            VkDescriptorSet renderSets[1] = { renderData_.rdAvengBasicTerrainDescriptorSets[currentFrameIndex] };
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                renderData_.rdAvengBasicTerrainPipelineLayout, 0, 1, renderSets, 0, nullptr);

            std::printf("rendering chunk {%d, %d}\n", coord.x, coord.z);

            //glm::vec3 worldOffset{
            //    coord.x * 256.0f,
            //    0.0f,
            //    coord.z * 256.0f
            //};

            glm::mat4 modelMat{ 1.0 };

            dpc.modelMat = modelMat;

            vkCmdPushConstants(
                cmd,
                renderData_.rdAvengBasicTerrainPipelineLayout, // or your unified layout, whichever created basicPipeline
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(VkBasicTerrainPushConstant),
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
            if (slot.state != TerrainRuntimeState::CpuReady) {
                continue;
            }

            // Upload and transfer state
            if (uploadTerrainChunkToGpu(slot)) {
                slot.gpu.valid = true;
                slot.cpuRenderable.reset();
            }
            else {
                slot.state = TerrainRuntimeState::Failed;
            }

            uploadsThisFrame++;

        }
    }

    bool TerrainController::uploadTerrainChunkToGpu(TerrainChunkSlot& slot) {

        // Ready but without a renderable
        if (!slot.cpuRenderable) {
            Logger::log(1, "%s error: slot has no cpu renderable\n", __FUNCTION__);
            return false;
        }

        slot.state = TerrainRuntimeState::Uploading;

        auto& cpu = *slot.cpuRenderable;
        auto& gpu = slot.gpu;

        gpu.draw.vertexCount = static_cast<uint32_t>(cpu.vbo.size());
        gpu.draw.indexCount = static_cast<uint32_t>(cpu.ibo.size());

        if (!VertexBuffer::init(engineDevice_, gpu.draw.vertexBuffer, cpu.vbo.size() * sizeof(glm::vec3))) {
            Logger::log(1, "Failed to init Vertex buffer\n");
            return false;
        }

        if (!IndexBuffer::init(engineDevice_, gpu.draw.indexBuffer, cpu.ibo.size() * sizeof(uint32_t))) {
            Logger::log(1, "Failed to init Index buffer\n");
            return false;
        }

        // ---- graphics buffers ----
        if (!VertexBuffer::uploadData(engineDevice_, gpu.draw.vertexBuffer, cpu.vbo)) {
            Logger::log(1, "Failed to upload Vertex Buffer\n");
            // TODO cleanupGpuChunk(gpu);
            return false;
        }

        if (!IndexBuffer::uploadData(engineDevice_, gpu.draw.indexBuffer, cpu.ibo)) {
            Logger::log(1, "Failed to upload Index Buffer\n");
            // TODO cleanupGpuChunk(gpu);
            return false;
        }

        // Promote to resident state
        slot.state = TerrainRuntimeState::Resident;
    }

    //std::unique_ptr<procgen::TerrainRenderable> const& TerrainController::lastRequestedRenderable() const noexcept {
    //    return lastRequested_;
    //}

    ChunkCoord TerrainController::offsetCoord(ChunkCoord base, int dx, int dz) noexcept {
        // Assumes ChunkCoord has members named x and z.
        // If yours are different, adjust here.
        base.x += dx;
        base.z += dz;
        return base;
    }

    void TerrainController::ensureChunkRequested(ChunkCoord coord)
    {
        auto& slot = slots_[coord];
        slot.coord = coord;

        if (slot.state == TerrainRuntimeState::Requested ||
            slot.state == TerrainRuntimeState::CpuReady ||
            slot.state == TerrainRuntimeState::Uploading ||
            slot.state == TerrainRuntimeState::Resident)
        {
            return;
        }

        slot.requestId = chunks_->requestRenderableAsync(coord, frameIndex_);
        slot.state = TerrainRuntimeState::Requested;
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
                slot.state = TerrainRuntimeState::Failed;
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
            slot.state = TerrainRuntimeState::CpuReady;
        });
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



}