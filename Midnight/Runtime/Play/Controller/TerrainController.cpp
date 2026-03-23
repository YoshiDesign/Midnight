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

namespace {
    VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }
}

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

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        for (auto const& [coord, slot] : slots_) {
            if (slot.state != TerrainRuntimeState::Resident) {
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
    
    void TerrainController::renderDebug(VkCommandBuffer cmd, VkPipeline pipeline, int currentFrameIndex)
    {

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData_.rdAvengEditorBasicTerrainPipeline);

        VkDescriptorSet renderSets[1] = { renderData_.rdAvengBasicTerrainDescriptorSets[currentFrameIndex] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderData_.rdAvengBasicTerrainPipelineLayout, 
                                0, 1, renderSets, 0, nullptr);

        for (auto const& [coord, slot] : slots_) {
            if (slot.state != TerrainRuntimeState::Resident) {
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

    bool TerrainController::uploadTerrainChunkToGpu(TerrainChunkSlot& slot) {

        if (!slot.cpuRenderable) {
            Logger::log(1, "%s error: slot has no cpu renderable\n", __FUNCTION__);
            return false;
        }

        if (settingsUboBuffer_ == VK_NULL_HANDLE) {
            Logger::log(1, "%s error: terrain settings UBO not set (call setTerrainSettingsUbo first)\n", __FUNCTION__);
            return false;
        }

        slot.state = TerrainRuntimeState::Uploading;

        auto& cpu = *slot.cpuRenderable;
        auto& gpu = slot.gpu;

        // ---- Graphics buffers (VBO / IBO) ----
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

        if (!VertexBuffer::uploadData(engineDevice_, gpu.draw.vertexBuffer, cpu.vbo)) {
            Logger::log(1, "Failed to upload Vertex Buffer\n");
            return false;
        }

        if (!IndexBuffer::uploadData(engineDevice_, gpu.draw.indexBuffer, cpu.ibo)) {
            Logger::log(1, "Failed to upload Index Buffer\n");
            return false;
        }

        // ---- Compute SSBOs ----
        const VkDeviceSize ssboAlign = engineDevice_.properties.limits.minStorageBufferOffsetAlignment;

        const uint32_t totalVerts = static_cast<uint32_t>(cpu.packedPositions.size());
        const uint32_t coreVerts  = cpu.alignment.countCorePosition;
        const uint32_t totalTris  = static_cast<uint32_t>(cpu.packedTriangles.size());

        gpu.packed.totalVerts = totalVerts;
        gpu.packed.coreVerts  = coreVerts;
        gpu.packed.totalTris  = totalTris;

        // Input SSBO layout: [positions(vec4) | triangles(vec3/uvec3) | adjacency]
        const VkDeviceSize positionsSize  = totalVerts * sizeof(glm::vec4);
        const VkDeviceSize trianglesSize  = totalTris  * sizeof(glm::vec3);  // uvec3 bit-pattern through vec3
        const VkDeviceSize adjacencySize  = totalVerts * sizeof(procgen::VertexAdjacency);

        gpu.packed.positionsOffset = 0;
        gpu.packed.trianglesOffset = alignUp(positionsSize, ssboAlign);
        gpu.packed.adjacencyOffset = alignUp(gpu.packed.trianglesOffset + trianglesSize, ssboAlign);

        const VkDeviceSize inputTotalSize = gpu.packed.adjacencyOffset + adjacencySize;

        if (!ShaderStorageBuffer::init(engineDevice_, gpu.packed.inputSsbo, MapMode::OnDemand, ResidentMode::CPU, inputTotalSize)) {
            Logger::log(1, "%s error: could not create terrain input SSBO\n", __FUNCTION__);
            return false;
        }

        // Map once and write all three regions at their aligned byte offsets.
        // We can't use uploadSsboDataRange here because aligned offsets may not
        // divide evenly by element size (e.g. vec3 = 12 bytes vs 256-byte alignment).
        {
            void* mapped = nullptr;
            VkResult mapResult = vmaMapMemory(engineDevice_.allocator(), gpu.packed.inputSsbo.bufferAlloc, &mapped);
            if (mapResult != VK_SUCCESS) {
                Logger::log(1, "%s error: could not map terrain input SSBO\n", __FUNCTION__);
                return false;
            }

            auto* base = static_cast<std::byte*>(mapped);
            std::memcpy(base + gpu.packed.positionsOffset,  cpu.packedPositions.data(),  positionsSize);
            std::memcpy(base + gpu.packed.trianglesOffset,  cpu.packedTriangles.data(),  trianglesSize);
            std::memcpy(base + gpu.packed.adjacencyOffset,  cpu.packedAdjacency.data(),  adjacencySize);

            if (!gpu.packed.inputSsbo.isHostCoherent) {
                vmaFlushAllocation(engineDevice_.allocator(), gpu.packed.inputSsbo.bufferAlloc, 0, inputTotalSize);
            }

            vmaUnmapMemory(engineDevice_.allocator(), gpu.packed.inputSsbo.bufferAlloc);
        }

        // Output SSBO layout: [normals(vec4) | steepness(float) | weights(vec4)]
        // Sized for core vertices only (compute only writes core outputs)
        const VkDeviceSize normalsSize   = coreVerts * sizeof(glm::vec4);
        const VkDeviceSize steepnessSize = coreVerts * sizeof(float);
        const VkDeviceSize weightsSize   = coreVerts * sizeof(glm::vec4);

        gpu.packed.normalsOffset   = 0;
        gpu.packed.steepnessOffset = alignUp(normalsSize, ssboAlign);
        gpu.packed.weightsOffset   = alignUp(gpu.packed.steepnessOffset + steepnessSize, ssboAlign);

        const VkDeviceSize outputTotalSize = gpu.packed.weightsOffset + weightsSize;

        if (!ShaderStorageBuffer::init(engineDevice_, gpu.packed.outputSsbo, MapMode::GpuOnly, ResidentMode::GPU, outputTotalSize)) {
            Logger::log(1, "%s error: could not create terrain output SSBO\n", __FUNCTION__);
            return false;
        }

        // Write compute descriptor set for this chunk
        if (!writeChunkComputeDescriptorSet(
            gpu.packed,
            settingsUboBuffer_,
            settingsUboSize_))
        {
            Logger::log(1, "%s error: failed to write compute descriptor set\n", __FUNCTION__);
            return false;
        }

        // Write lit graphics descriptor set (binds compute output SSBOs for the vertex shader)
        if (!writeChunkGraphicsDescriptorSet(gpu.packed))
        {
            Logger::log(1, "%s error: failed to write graphics descriptor set\n", __FUNCTION__);
            return false;
        }

        slot.state = TerrainRuntimeState::Resident;
        return true;
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

    bool TerrainController::writeChunkComputeDescriptorSet(
        procgen::TerrainPackedGpuData& packed,
        VkBuffer settingsUboBuffer,
        VkDeviceSize settingsUboSize)
    {
        // Allocate a descriptor set from the terrain compute pool
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = renderData_.avengTerrainComputeDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &renderData_.rdTerrainComputeDescriptorSetLayout;

        VkResult result = vkAllocateDescriptorSets(engineDevice_.device(), &allocInfo, &packed.computeDescriptorSet);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not allocate terrain compute descriptor set (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        // All input bindings reference inputSsbo at their aligned offsets
        // All output bindings reference outputSsbo at their aligned offsets

        VkDescriptorBufferInfo settingsInfo{};
        settingsInfo.buffer = settingsUboBuffer;
        settingsInfo.offset = 0;
        settingsInfo.range  = settingsUboSize;

        const uint32_t totalVerts = packed.totalVerts;
        const uint32_t totalTris  = packed.totalTris;
        const uint32_t coreVerts  = packed.coreVerts;

        VkDescriptorBufferInfo adjacencyInfo{};
        adjacencyInfo.buffer = packed.inputSsbo.buffer;
        adjacencyInfo.offset = packed.adjacencyOffset;
        adjacencyInfo.range  = totalVerts * sizeof(procgen::VertexAdjacency);

        VkDescriptorBufferInfo trianglesInfo{};
        trianglesInfo.buffer = packed.inputSsbo.buffer;
        trianglesInfo.offset = packed.trianglesOffset;
        trianglesInfo.range  = totalTris * sizeof(glm::vec3);

        VkDescriptorBufferInfo positionsInfo{};
        positionsInfo.buffer = packed.inputSsbo.buffer;
        positionsInfo.offset = packed.positionsOffset;
        positionsInfo.range  = totalVerts * sizeof(glm::vec4);

        VkDescriptorBufferInfo normalsInfo{};
        normalsInfo.buffer = packed.outputSsbo.buffer;
        normalsInfo.offset = packed.normalsOffset;
        normalsInfo.range  = coreVerts * sizeof(glm::vec4);

        VkDescriptorBufferInfo steepnessInfo{};
        steepnessInfo.buffer = packed.outputSsbo.buffer;
        steepnessInfo.offset = packed.steepnessOffset;
        steepnessInfo.range  = coreVerts * sizeof(float);

        VkDescriptorBufferInfo weightsInfo{};
        weightsInfo.buffer = packed.outputSsbo.buffer;
        weightsInfo.offset = packed.weightsOffset;
        weightsInfo.range  = coreVerts * sizeof(glm::vec4);

        VkWriteDescriptorSet writes[7] = {};

        // Binding 0: SettingsUBO
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = packed.computeDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &settingsInfo;

        // Binding 2: Adjacency
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = packed.computeDescriptorSet;
        writes[1].dstBinding = 2;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &adjacencyInfo;

        // Binding 3: Triangles
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = packed.computeDescriptorSet;
        writes[2].dstBinding = 3;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &trianglesInfo;

        // Binding 4: Positions
        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = packed.computeDescriptorSet;
        writes[3].dstBinding = 4;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[3].descriptorCount = 1;
        writes[3].pBufferInfo = &positionsInfo;

        // Binding 5: VertexNormals (output)
        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = packed.computeDescriptorSet;
        writes[4].dstBinding = 5;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[4].descriptorCount = 1;
        writes[4].pBufferInfo = &normalsInfo;

        // Binding 6: Steepness (output)
        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = packed.computeDescriptorSet;
        writes[5].dstBinding = 6;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo = &steepnessInfo;

        // Binding 7: Weights (output)
        writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[6].dstSet = packed.computeDescriptorSet;
        writes[6].dstBinding = 7;
        writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[6].descriptorCount = 1;
        writes[6].pBufferInfo = &weightsInfo;

        vkUpdateDescriptorSets(engineDevice_.device(), 7, writes, 0, nullptr);
        return true;
    }

    bool TerrainController::writeChunkGraphicsDescriptorSet(procgen::TerrainPackedGpuData& packed)
    {
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = renderData_.avengTerrainLitDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &renderData_.rdTerrainLitGraphicsDescriptorSetLayout;

        VkResult result = vkAllocateDescriptorSets(engineDevice_.device(), &allocInfo, &packed.graphicsDescriptorSet);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not allocate terrain lit graphics descriptor set (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        const uint32_t coreVerts = packed.coreVerts;

        VkDescriptorBufferInfo normalsInfo{};
        normalsInfo.buffer = packed.outputSsbo.buffer;
        normalsInfo.offset = packed.normalsOffset;
        normalsInfo.range  = coreVerts * sizeof(glm::vec4);

        VkDescriptorBufferInfo weightsInfo{};
        weightsInfo.buffer = packed.outputSsbo.buffer;
        weightsInfo.offset = packed.weightsOffset;
        weightsInfo.range  = coreVerts * sizeof(glm::vec4);

        VkDescriptorBufferInfo steepnessInfo{};
        steepnessInfo.buffer = packed.outputSsbo.buffer;
        steepnessInfo.offset = packed.steepnessOffset;
        steepnessInfo.range  = coreVerts * sizeof(float);

        VkWriteDescriptorSet writes[3] = {};

        // Binding 0: VertexNormals
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = packed.graphicsDescriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &normalsInfo;

        // Binding 1: VertexWeights
        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = packed.graphicsDescriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[1].descriptorCount = 1;
        writes[1].pBufferInfo = &weightsInfo;

        // Binding 2: VertexSteepness
        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = packed.graphicsDescriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[2].descriptorCount = 1;
        writes[2].pBufferInfo = &steepnessInfo;

        vkUpdateDescriptorSets(engineDevice_.device(), 3, writes, 0, nullptr);
        return true;
    }

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

    void TerrainController::cleanupOne(TerrainChunkSlot& slot) {
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