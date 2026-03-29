#pragma once
#include "vulkan/vulkan_core.h"
#include "BasicTerrainAsset.h"
#include "CoreVK/VkRenderData.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "Utils/Logger.h"

/* 
* Technically we could merge terrain with the static model pipeline 
* to avoid binding an additional pipeline per frame.
* This would be best to do in tandem with a shift to bindless rendering
* for terrain assets since we already do so with the static & animated pipelines
* 
* TODO: Make a TerrainRenderSystem so we can extract all of the render() methods from TerrainController
* and maybe implement these (below) within it
*/

namespace {
    VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }
}

namespace aveng {

    struct TerrainUploadBatch {
        VkFence fence = VK_NULL_HANDLE;
        VkCommandBuffer cmdBuffer = VK_NULL_HANDLE;
        std::vector<ChunkCoord> inFlightSlots;
        bool active = false;
    };

    struct DeferredGpuCleanup {
        VkVertexBufferData vertexBuffer{};
        VkIndexBufferData indexBuffer{};
        VkShaderStorageBufferData inputSsbo{};
        VkShaderStorageBufferData outputSsbo{};
        VkDescriptorSet graphicsDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet computeDescriptorSet = VK_NULL_HANDLE;
        uint64_t retireFrame = 0;
    };

    inline bool writeChunkGraphicsDescriptorSet(
        EngineDevice& engineDevice_,
        VkRenderData& renderData_,
        procgen::TerrainPackedGpuData& packed
    )
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
        normalsInfo.range = coreVerts * sizeof(glm::vec4);

        VkDescriptorBufferInfo weightsInfo{};
        weightsInfo.buffer = packed.outputSsbo.buffer;
        weightsInfo.offset = packed.weightsOffset;
        weightsInfo.range = coreVerts * sizeof(glm::vec4);

        VkDescriptorBufferInfo steepnessInfo{};
        steepnessInfo.buffer = packed.outputSsbo.buffer;
        steepnessInfo.offset = packed.steepnessOffset;
        steepnessInfo.range = coreVerts * sizeof(float);

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

	inline bool writeChunkComputeDescriptorSet(
        EngineDevice& engineDevice_,
        VkRenderData& renderData_,
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
        settingsInfo.range = settingsUboSize;

        const uint32_t totalVerts = packed.totalVerts;
        const uint32_t totalTris = packed.totalTris;
        const uint32_t coreVerts = packed.coreVerts;

        VkDescriptorBufferInfo adjacencyInfo{};
        adjacencyInfo.buffer = packed.inputSsbo.buffer;
        adjacencyInfo.offset = packed.adjacencyOffset;
        adjacencyInfo.range = totalVerts * sizeof(procgen::VertexAdjacency);

        VkDescriptorBufferInfo trianglesInfo{};
        trianglesInfo.buffer = packed.inputSsbo.buffer;
        trianglesInfo.offset = packed.trianglesOffset;
        trianglesInfo.range = totalTris * sizeof(glm::vec3);

        VkDescriptorBufferInfo positionsInfo{};
        positionsInfo.buffer = packed.inputSsbo.buffer;
        positionsInfo.offset = packed.positionsOffset;
        positionsInfo.range = totalVerts * sizeof(glm::vec4);

        VkDescriptorBufferInfo normalsInfo{};
        normalsInfo.buffer = packed.outputSsbo.buffer;
        normalsInfo.offset = packed.normalsOffset;
        normalsInfo.range = coreVerts * sizeof(glm::vec4);

        VkDescriptorBufferInfo steepnessInfo{};
        steepnessInfo.buffer = packed.outputSsbo.buffer;
        steepnessInfo.offset = packed.steepnessOffset;
        steepnessInfo.range = coreVerts * sizeof(float);

        VkDescriptorBufferInfo weightsInfo{};
        weightsInfo.buffer = packed.outputSsbo.buffer;
        weightsInfo.offset = packed.weightsOffset;
        weightsInfo.range = coreVerts * sizeof(glm::vec4);

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


    // Creates GPU-local + staging buffers, fills staging via memcpy, sets up SSBOs and
    // descriptor sets. Does NOT record any copy commands or change slot state.
    inline bool prepareChunkUpload(
        EngineDevice& engineDevice_,
        VkRenderData& renderData_,
        procgen::TerrainChunkSlot& slot,
        VkBuffer settingsUboBuffer_,
        VkDeviceSize settingsUboSize_)
    {
        if (!slot.cpuRenderable) {
            Logger::log(1, "%s error: slot has no cpu renderable\n", __FUNCTION__);
            return false;
        }

        if (settingsUboBuffer_ == VK_NULL_HANDLE) {
            Logger::log(1, "%s error: terrain settings UBO not set (call setTerrainSettingsUbo first)\n", __FUNCTION__);
            return false;
        }

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

        if (!VertexBuffer::fillStaging(engineDevice_, gpu.draw.vertexBuffer, cpu.vbo)) {
            Logger::log(1, "Failed to fill VBO staging\n");
            return false;
        }

        if (!IndexBuffer::fillStaging(engineDevice_, gpu.draw.indexBuffer, cpu.ibo)) {
            Logger::log(1, "Failed to fill IBO staging\n");
            return false;
        }

        // ---- Compute SSBOs (CPU-visible, no staging copy needed) ----
        const VkDeviceSize ssboAlign = engineDevice_.properties.limits.minStorageBufferOffsetAlignment;

        const uint32_t totalVerts = static_cast<uint32_t>(cpu.packedPositions.size());
        const uint32_t coreVerts = cpu.alignment.countCorePosition;
        const uint32_t totalTris = static_cast<uint32_t>(cpu.packedTriangles.size());

        gpu.packed.totalVerts = totalVerts;
        gpu.packed.coreVerts = coreVerts;
        gpu.packed.totalTris = totalTris;

        const VkDeviceSize positionsSize = totalVerts * sizeof(glm::vec4);
        const VkDeviceSize trianglesSize = totalTris * sizeof(glm::vec3);
        const VkDeviceSize adjacencySize = totalVerts * sizeof(procgen::VertexAdjacency);

        gpu.packed.positionsOffset = 0;
        gpu.packed.trianglesOffset = alignUp(positionsSize, ssboAlign);
        gpu.packed.adjacencyOffset = alignUp(gpu.packed.trianglesOffset + trianglesSize, ssboAlign);

        const VkDeviceSize inputTotalSize = gpu.packed.adjacencyOffset + adjacencySize;

        if (!ShaderStorageBuffer::init(engineDevice_, gpu.packed.inputSsbo, MapMode::OnDemand, ResidentMode::CPU, inputTotalSize)) {
            Logger::log(1, "%s error: could not create terrain input SSBO\n", __FUNCTION__);
            return false;
        }

        {
            void* mapped = nullptr;
            VkResult mapResult = vmaMapMemory(engineDevice_.allocator(), gpu.packed.inputSsbo.bufferAlloc, &mapped);
            if (mapResult != VK_SUCCESS) {
                Logger::log(1, "%s error: could not map terrain input SSBO\n", __FUNCTION__);
                return false;
            }

            auto* base = static_cast<std::byte*>(mapped);
            std::memcpy(base + gpu.packed.positionsOffset, cpu.packedPositions.data(), positionsSize);
            std::memcpy(base + gpu.packed.trianglesOffset, cpu.packedTriangles.data(), trianglesSize);
            std::memcpy(base + gpu.packed.adjacencyOffset, cpu.packedAdjacency.data(), adjacencySize);

            if (!gpu.packed.inputSsbo.isHostCoherent) {
                vmaFlushAllocation(engineDevice_.allocator(), gpu.packed.inputSsbo.bufferAlloc, 0, inputTotalSize);
            }

            vmaUnmapMemory(engineDevice_.allocator(), gpu.packed.inputSsbo.bufferAlloc);
        }

        const VkDeviceSize normalsSize = coreVerts * sizeof(glm::vec4);
        const VkDeviceSize steepnessSize = coreVerts * sizeof(float);
        const VkDeviceSize weightsSize = coreVerts * sizeof(glm::vec4);

        gpu.packed.normalsOffset = 0;
        gpu.packed.steepnessOffset = alignUp(normalsSize, ssboAlign);
        gpu.packed.weightsOffset = alignUp(gpu.packed.steepnessOffset + steepnessSize, ssboAlign);

        const VkDeviceSize outputTotalSize = gpu.packed.weightsOffset + weightsSize;

        if (!ShaderStorageBuffer::init(engineDevice_, gpu.packed.outputSsbo, MapMode::GpuOnly, ResidentMode::GPU, outputTotalSize)) {
            Logger::log(1, "%s error: could not create terrain output SSBO\n", __FUNCTION__);
            return false;
        }

        if (!writeChunkComputeDescriptorSet(engineDevice_, renderData_, gpu.packed, settingsUboBuffer_, settingsUboSize_)) {
            Logger::log(1, "%s error: failed to write compute descriptor set\n", __FUNCTION__);
            return false;
        }

        if (!writeChunkGraphicsDescriptorSet(engineDevice_, renderData_, gpu.packed)) {
            Logger::log(1, "%s error: failed to write graphics descriptor set\n", __FUNCTION__);
            return false;
        }

        return true;
    }

    inline void submitTerrainQueue(EngineDevice& engineDevice, TerrainUploadBatch& uploadBatch) {
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;

        vkCmdPipelineBarrier(
            uploadBatch.cmdBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
            0, 1, &barrier, 0, nullptr, 0, nullptr
        );

        vkEndCommandBuffer(uploadBatch.cmdBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &uploadBatch.cmdBuffer;

        vkQueueSubmit(engineDevice.graphicsQueue(), 1, &submitInfo, uploadBatch.fence);
        uploadBatch.active = true;

    }

    // Records VBO and IBO staging-to-device copy commands into the provided command buffer.
    // Barriers are the caller's responsibility (batched at the end of all copies).
    inline void recordChunkCopies(VkCommandBuffer cmd, procgen::TerrainChunkSlot& slot) {
        VertexBuffer::recordCopy(cmd, slot.gpu.draw.vertexBuffer);
        IndexBuffer::recordCopy(cmd, slot.gpu.draw.indexBuffer);
    }

}