#pragma once
#include "vulkan/vulkan_core.h"
#include "BasicTerrainAsset.h"
#include "TerrainResourcePool.h"
#include "CoreVK/VkRenderData.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VertexBuffer.h"
#include "CoreVK/IndexBuffer.h"
#include "CoreVK/AvengStorageBuffer.h"
#include "Utils/Logger.h"
#include "Utils/Timer.h"

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
        std::vector<uint32_t> inFlightSlots;
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
        EngineDevice& engineDevice,
        VkRenderData& renderData,
        procgen::TerrainPackedGpuData& packed
    )
    {
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = renderData.avengTerrainLitDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &renderData.rdTerrainLitGraphicsDescriptorSetLayout;

        VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &allocInfo, &packed.graphicsDescriptorSet);
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

        vkUpdateDescriptorSets(engineDevice.device(), 3, writes, 0, nullptr);
        return true;
    }

    // TODO : I think this only needs to happen once since all terrain share the same descriptors.
    // They don't each need their own, do they? I could be wrong here. Each 3x3 renderable and its
    // support region are packed into 1 buffer, we haven't implemented the giga-buffer where all 
    // Terrain data that can be rendered is in 1 giant buffer.
	inline bool writeChunkComputeDescriptorSet(
        EngineDevice& engineDevice,
        VkRenderData& renderData,
        procgen::TerrainPackedGpuData& packed,
        VkBuffer settingsUboBuffer,
        VkDeviceSize settingsUboSize)
    {
        // Allocate a descriptor set from the terrain compute pool
        VkDescriptorSetAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocInfo.descriptorPool = renderData.avengTerrainComputeDescriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &renderData.rdTerrainComputeDescriptorSetLayout;

        VkResult result = vkAllocateDescriptorSets(engineDevice.device(), &allocInfo, &packed.computeDescriptorSet);
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

        vkUpdateDescriptorSets(engineDevice.device(), 7, writes, 0, nullptr);
        return true;
    }


    // Creates GPU-local + staging buffers (or acquires recycled ones from pool),
    // fills staging via memcpy, sets up SSBOs and descriptor sets.
    // Does NOT record any copy commands or change slot state.
    inline bool prepareChunkUpload(
        EngineDevice& engineDevice,
        VkRenderData& renderData,
        procgen::TerrainChunkSlot& slot,
        VkBuffer settingsUboBuffer_,
        VkDeviceSize settingsUboSize_,
        TerrainResourcePool& pool)
    {
        if (settingsUboBuffer_ == VK_NULL_HANDLE) {
            Logger::log(1, "%s error: terrain settings UBO not set (call setTerrainSettingsUbo first)\n", __FUNCTION__);
            return false;
        }

        auto& cpu = slot.renderable;
        auto& gpu = slot.gpu;

        gpu.draw.vertexCount = static_cast<uint32_t>(cpu.vbo.size());
        gpu.draw.indexCount = static_cast<uint32_t>(cpu.ibo.size());

        const uint32_t vboSize = cpu.vbo.size() * sizeof(glm::vec3);
        const size_t iboSize = cpu.ibo.size() * sizeof(uint32_t);

#ifdef M_DEBUG
  //      Timer vboIboTimer{};
  //      Timer ssbo1Timer{};
		//Timer ssbo2Timer{};
		//Timer descriptor1Timer{};
		//Timer descriptor2Timer{};
  //      vboIboTimer.start();
#endif

        // VBO: try pool first, fall back to init
        if (!pool.vbo.empty() && pool.vbo.back().bufferSize >= vboSize) {
            //Logger::log(1, "Initializing New VBO...........\n");
            gpu.draw.vertexBuffer = pool.vbo.back();
            pool.vbo.pop_back();
        } else {

            if (!pool.vbo.empty() && pool.vbo.size() > 3) {
                pool.vbo.pop_back();
            }

            if (!VertexBuffer::init(engineDevice, gpu.draw.vertexBuffer, vboSize)) {
                Logger::log(1, "Failed to init Vertex buffer\n");
                return false;
            }
        }

        // IBO: try pool first, fall back to init
        if (!pool.ibo.empty() && pool.ibo.back().bufferSize >= iboSize) {
            // Logger::log(1, "Initializing New IBO..........\n");
            gpu.draw.indexBuffer = pool.ibo.back();
            pool.ibo.pop_back();
        } else {

            if (!pool.ibo.empty() && pool.ibo.size() > 3) {
                pool.ibo.pop_back();
            }

            if (!IndexBuffer::init(engineDevice, gpu.draw.indexBuffer, iboSize)) {
                Logger::log(1, "Failed to init Index buffer\n");
                return false;
            }
        }

        if (!VertexBuffer::fillStaging(engineDevice, gpu.draw.vertexBuffer, cpu.vbo)) {
            Logger::log(1, "Failed to fill VBO staging\n");
            return false;
        }

        if (!IndexBuffer::fillStaging(engineDevice, gpu.draw.indexBuffer, cpu.ibo)) {
            Logger::log(1, "Failed to fill IBO staging\n");
            return false;
        }

#ifdef M_DEBUG
        /*renderData.rdTerrainVboIboTime = vboIboTimer.stop();
		if (renderData.rdTerrainVboIboTime > renderData.rdTerrainVboIboTimeMAX) {
            renderData.rdTerrainVboIboTimeMAX = renderData.rdTerrainVboIboTime;
        }*/
        /*assert(cpu.alignment.baseCorePosition == 0  && "[1] missing cpu alignment value baseCorePosition");
        assert(cpu.alignment.countCorePosition > 0  && "[2] missing cpu alignment value countCorePosition");
        assert(cpu.alignment.countHaloPosition > 0  && "[3] missing cpu alignment value countHaloPosition");
        assert(cpu.alignment.baseCoreTriangle == 0  && "[4] missing cpu alignment value baseCoreTriangle");
        assert(cpu.alignment.countCoreTriangle > 0  && "[5] missing cpu alignment value countCoreTriangle");
        assert(cpu.alignment.countHaloTriangle > 0  && "[6] missing cpu alignment value countHaloTriangle");
        assert(cpu.alignment.baseCoreAdjacency == 0 && "[7] missing cpu alignment value baseCoreAdjacency");
        assert(cpu.alignment.countCoreAdjacency > 0 && "[8] missing cpu alignment value countCoreAdjacency");
        assert(cpu.alignment.countHaloAdjacency > 0 && "[9] missing cpu alignment value countHaloAdjacency");*/
#endif

        // ---- Compute SSBOs (CPU-visible, no staging copy needed) ----
        const VkDeviceSize ssboAlign = engineDevice.properties.limits.minStorageBufferOffsetAlignment;

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

//// #region agent log
//        auto _dbg_t0 = std::chrono::steady_clock::now();
//        auto _dbg_t1 = _dbg_t0, _dbg_t2 = _dbg_t0;
//        bool _dbg_poolHit1 = false;
//        size_t _dbg_poolSz1 = pool.inputSsbo.size();
//        size_t _dbg_backSz1 = (!pool.inputSsbo.empty()) ? pool.inputSsbo.back().bufferSize : 0;
//// #endregion
//
//#ifdef M_DEBUG
//        ssbo1Timer.start();
//#endif

        // Input SSBO: try pool first, fall back to init
        if (!pool.inputSsbo.empty() && pool.inputSsbo.back().bufferSize >= inputTotalSize) {
            gpu.packed.inputSsbo = pool.inputSsbo.back();
            pool.inputSsbo.pop_back();
            // _dbg_poolHit1 = true; // agent log
        } else {

            if (!pool.inputSsbo.empty() && pool.inputSsbo.size() > 3) {
                pool.inputSsbo.pop_back();
            }

            // Logger::log(1, "Initializing New SSBO..........\n");
            if (!ShaderStorageBuffer::init(engineDevice, gpu.packed.inputSsbo, MapMode::OnDemand, ResidentMode::CPU, inputTotalSize)) {
                Logger::log(1, "%s error: could not create terrain input SSBO\n", __FUNCTION__);
                return false;
            }
        }

        // _dbg_t1 = std::chrono::steady_clock::now(); // agent log

        {
            void* mapped = nullptr;
            VkResult mapResult = vmaMapMemory(engineDevice.allocator(), gpu.packed.inputSsbo.bufferAlloc, &mapped);
            if (mapResult != VK_SUCCESS) {
                Logger::log(1, "%s error: could not map terrain input SSBO\n", __FUNCTION__);
                return false;
            }

            // _dbg_t2 = std::chrono::steady_clock::now(); // agent log

            auto* base = static_cast<std::byte*>(mapped);
            std::memcpy(base + gpu.packed.positionsOffset, cpu.packedPositions.data(), positionsSize);
            std::memcpy(base + gpu.packed.trianglesOffset, cpu.packedTriangles.data(), trianglesSize);
            std::memcpy(base + gpu.packed.adjacencyOffset, cpu.packedAdjacency.data(), adjacencySize);

            if (!gpu.packed.inputSsbo.isHostCoherent) {
                vmaFlushAllocation(engineDevice.allocator(), gpu.packed.inputSsbo.bufferAlloc, 0, inputTotalSize);
            }

            vmaUnmapMemory(engineDevice.allocator(), gpu.packed.inputSsbo.bufferAlloc);

//// #region agent log
//            { auto _t3 = std::chrono::steady_clock::now();
//              float _initMs = std::chrono::duration<float,std::milli>(_dbg_t1-_dbg_t0).count();
//              float _mapMs  = std::chrono::duration<float,std::milli>(_dbg_t2-_dbg_t1).count();
//              float _cpyMs  = std::chrono::duration<float,std::milli>(_t3-_dbg_t2).count();
//              float _totMs  = std::chrono::duration<float,std::milli>(_t3-_dbg_t0).count();
//              FILE* _f;
//              fopen_s(&_f, "c:/Users/Yoshi/dev/Midnight/debug-6bde4a.log", "a");
//              if(_f){ std::fprintf(_f,"{\"sessionId\":\"6bde4a\",\"hypothesisId\":\"A\",\"location\":\"VkTerrain.h:393\",\"message\":\"ssbo1 breakdown\",\"data\":{\"poolHit\":%s,\"poolSize\":%zu,\"backBufSize\":%zu,\"needed\":%llu,\"initOrPoolMs\":%.3f,\"mapMs\":%.3f,\"cpyFlushMs\":%.3f,\"totalMs\":%.3f},\"timestamp\":%lld}\n",_dbg_poolHit1?"true":"false",_dbg_poolSz1,_dbg_backSz1,(unsigned long long)inputTotalSize,_initMs,_mapMs,_cpyMs,_totMs,(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); std::fclose(_f);} }
//// #endregion
        }
//
//#ifdef M_DEBUG
//		renderData.rdTerrainSsbo1Time = ssbo1Timer.stop();
//        if (renderData.rdTerrainSsbo1Time > renderData.rdTerrainSsbo1TimeMAX) {
//			renderData.rdTerrainSsbo1TimeMAX = renderData.rdTerrainSsbo1Time;
//        }
//#endif

        const VkDeviceSize normalsSize = coreVerts * sizeof(glm::vec4);
        const VkDeviceSize steepnessSize = coreVerts * sizeof(float);
        const VkDeviceSize weightsSize = coreVerts * sizeof(glm::vec4);

        gpu.packed.normalsOffset = 0;
        gpu.packed.steepnessOffset = alignUp(normalsSize, ssboAlign);
        gpu.packed.weightsOffset = alignUp(gpu.packed.steepnessOffset + steepnessSize, ssboAlign);

        const VkDeviceSize outputTotalSize = gpu.packed.weightsOffset + weightsSize;

//// #region agent log
//        auto _dbg_s2_t0 = std::chrono::steady_clock::now();
//        bool _dbg_poolHit2 = false;
//        size_t _dbg_poolSz2 = pool.outputSsbo.size();
//        size_t _dbg_backSz2 = (!pool.outputSsbo.empty()) ? pool.outputSsbo.back().bufferSize : 0;
//// #endregion

//#ifdef M_DEBUG
//        ssbo2Timer.start();
//#endif

        // Output SSBO: try pool first (GPU-only, no mapping needed), fall back to init
        if (!pool.outputSsbo.empty() && pool.outputSsbo.back().bufferSize >= static_cast<size_t>(outputTotalSize)) {
            gpu.packed.outputSsbo = pool.outputSsbo.back();
            pool.outputSsbo.pop_back();
            // _dbg_poolHit2 = true; // agent log
        } else {

            if (!pool.outputSsbo.empty() && pool.outputSsbo.size() > 3) {
                pool.outputSsbo.pop_back();
            }

            Logger::log(1, "Initializing New Output SSBO..........\n");
            if (!ShaderStorageBuffer::init(engineDevice, gpu.packed.outputSsbo, MapMode::GpuOnly, ResidentMode::GPU, outputTotalSize)) {
                Logger::log(1, "%s error: could not create terrain output SSBO\n", __FUNCTION__);
                return false;
            }
        }
//
//// #region agent log
//        { auto _s2_t1 = std::chrono::steady_clock::now();
//          float _s2Ms = std::chrono::duration<float,std::milli>(_s2_t1-_dbg_s2_t0).count();
//          FILE* _f;
//          fopen_s(&_f, "c:/Users/Yoshi/dev/Midnight/debug-6bde4a.log", "a");
//          if(_f){ std::fprintf(_f,"{\"sessionId\":\"6bde4a\",\"hypothesisId\":\"B\",\"location\":\"VkTerrain.h:432\",\"message\":\"ssbo2 breakdown\",\"data\":{\"poolHit\":%s,\"poolSize\":%zu,\"backBufSize\":%zu,\"needed\":%llu,\"totalMs\":%.3f},\"timestamp\":%lld}\n",_dbg_poolHit2?"true":"false",_dbg_poolSz2,_dbg_backSz2,(unsigned long long)outputTotalSize,_s2Ms,(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); std::fclose(_f);} }
//// #endregion

//#ifdef M_DEBUG
//        renderData.rdTerrainSsbo2Time = ssbo2Timer.stop();
//        if (renderData.rdTerrainSsbo2Time > renderData.rdTerrainSsbo2TimeMAX) {
//            renderData.rdTerrainSsbo2TimeMAX = renderData.rdTerrainSsbo2Time;
//        }
//		descriptor1Timer.start();
//#endif

        if (!writeChunkComputeDescriptorSet(engineDevice, renderData, gpu.packed, settingsUboBuffer_, settingsUboSize_)) {
            Logger::log(1, "%s error: failed to write compute descriptor set\n", __FUNCTION__);
            return false;
        }

//#ifdef M_DEBUG
//		renderData.rdTerrainDescriptor1WriterTime = descriptor1Timer.stop();
//        if (renderData.rdTerrainDescriptor1WriterTime > renderData.rdTerrainDescriptor1WriterTimeMAX) {
//			renderData.rdTerrainDescriptor1WriterTimeMAX = renderData.rdTerrainDescriptor1WriterTime;
//        }
//
//        descriptor2Timer.start();
//#endif

        if (!writeChunkGraphicsDescriptorSet(engineDevice, renderData, gpu.packed)) {
            Logger::log(1, "%s error: failed to write graphics descriptor set\n", __FUNCTION__);
            return false;
        }

//#ifdef M_DEBUG
//		renderData.rdTerrainDescriptor2WriterTime = descriptor2Timer.stop();
//        if (renderData.rdTerrainDescriptor2WriterTime > renderData.rdTerrainDescriptor2WriterTimeMAX) {
//			renderData.rdTerrainDescriptor2WriterTimeMAX = renderData.rdTerrainDescriptor2WriterTime;
//        }
//
//#endif

        return true;
    }

    inline void submitTerrainQueue(
        EngineDevice& engineDevice, 
        TerrainUploadBatch& uploadBatch
#ifdef M_DEBUG
		,VkRenderData& renderData
#endif
    ) {

//#ifdef M_DEBUG
//		Timer queueSubmitTimer{};
//        queueSubmitTimer.start();
//#endif

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

//#ifdef M_DEBUG
//		renderData.rdqueueSubmitTimer = queueSubmitTimer.stop();
//        if (renderData.rdqueueSubmitTimer > renderData.rdqueueSubmitTimerMAX) {
//            renderData.rdqueueSubmitTimerMAX = renderData.rdqueueSubmitTimer;
//        }
//#endif

    }

    // Records VBO and IBO staging-to-device copy commands into the provided command buffer.
    // Barriers are the caller's responsibility (batched at the end of all copies).
    // V1: recordCopy copies the full bufferSize (allocation size), not the actual data size.
    // With recycled oversized buffers this copies stale trailing bytes -- safe because the
    // GPU only reads up to vertexCount/indexCount, but wastes a small amount of transfer bandwidth.
    inline void recordChunkCopies(VkCommandBuffer cmd, procgen::TerrainChunkSlot& slot) {
        VertexBuffer::recordCopy(cmd, slot.gpu.draw.vertexBuffer);
        IndexBuffer::recordCopy(cmd, slot.gpu.draw.indexBuffer);
    }

}