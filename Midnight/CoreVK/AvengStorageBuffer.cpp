#include "AvengStorageBuffer.h"

namespace aveng {
    bool ShaderStorageBuffer::init(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, MapMode mode, ResidentMode resMode, size_t bufferSize) {
        /* avoid errors with zero sized buffers */
        if (bufferSize == 0) {
            bufferSize = 5120;
        }

        assert((mode == MapMode::Persistent && resMode == ResidentMode::GPU) == false && "SSBO was initialized with misconfigured options.");
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        VmaAllocationCreateInfo vmaAllocInfo{};
        if (resMode == ResidentMode::GPU) { vmaAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE; }
        else { vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU; }

        if (mode == MapMode::Persistent) {
            /* Warning */
            // When using VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT never access the buffer randomly e.g.pMappedData[i] 
            // Prepare your data in a local variable and memcpy() it to the mapped pointer all at once.
            vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }

        if (mode == MapMode::GpuOnly || mode == MapMode::OnDemand) {
            vmaAllocInfo.flags = 0;
        }

        VmaAllocationInfo readInfo{};
        VkResult result = vmaCreateBuffer(engineDevice.allocator(), &bufferInfo, &vmaAllocInfo,
            &SSBOData.buffer, &SSBOData.bufferAlloc, &readInfo);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not allocate SSBO via VMA (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        SSBOData.bufferSize = bufferSize;                                              
        SSBOData.mapped = (mode == MapMode::Persistent) ? readInfo.pMappedData : nullptr; // readInfo.pMappedData will safely assign null, I'm just being explicit
        
        // If the buffer turns out to be host coherent, you won't need to flush it after data is uploaded
        if (mode != MapMode::GpuOnly) {
            VkMemoryPropertyFlags memFlags{};
            vmaGetAllocationMemoryProperties(engineDevice.allocator(), SSBOData.bufferAlloc, &memFlags);
            SSBOData.isHostCoherent = (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        }

        if (resMode == ResidentMode::GPU) Logger::log(1, "--- Created GPU-ONLY Resident!\n");
        //Logger::log(1, "%s: created SSBO of size %i\nResident of:\t%d\nCoherence:\t%d\n", __FUNCTION__, bufferSize, resMode, SSBOData.isHostCoherent);

        return true;
    }

    /* NOTE: The template was only ever used to determin the size. We can wrap most of this function's implementation to somewhat reduce duplication of the core logic */

    bool ShaderStorageBuffer::uploadSsboData(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, std::span<const glm::mat4> bufferData)
    {
        if (bufferData.empty()) {
            return false;
        }

        if ((bufferData.size() * sizeof(glm::mat4)) > SSBOData.bufferSize) {
            Logger::log(1, "%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, SSBOData.buffer, SSBOData.bufferSize, (bufferData.size() * sizeof(glm::mat4)));
            return true;
        }

        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), SSBOData.bufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map SSBO memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, bufferData.data(), (bufferData.size() * sizeof(glm::mat4)));

        if (!SSBOData.isHostCoherent) {
            vmaFlushAllocation(engineDevice.allocator(), SSBOData.bufferAlloc, 0, SSBOData.bufferSize);
        }

        vmaUnmapMemory(engineDevice.allocator(), SSBOData.bufferAlloc);
        return true;
    }

    bool ShaderStorageBuffer::uploadSsboData(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, std::span<const int32_t> bufferData)
    {
        if (bufferData.empty()) {
            return false;
        }

        if ((bufferData.size() * sizeof(int32_t)) > SSBOData.bufferSize) {
            Logger::log(1, "%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, SSBOData.buffer, SSBOData.bufferSize, (bufferData.size() * sizeof(int32_t)));
            return true;
        }

        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), SSBOData.bufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map SSBO memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, bufferData.data(), (bufferData.size() * sizeof(int32_t)));

        if (!SSBOData.isHostCoherent) {
            vmaFlushAllocation(engineDevice.allocator(), SSBOData.bufferAlloc, 0, SSBOData.bufferSize);
        }

        vmaUnmapMemory(engineDevice.allocator(), SSBOData.bufferAlloc);
        return true;
    }

    bool ShaderStorageBuffer::uploadSsboData(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, std::span<const glm::vec2> bufferData)
    {
        if (bufferData.empty()) {
            return false;
        }

        if ((bufferData.size() * sizeof(glm::vec2)) > SSBOData.bufferSize) {
            Logger::log(1, "%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, SSBOData.buffer, SSBOData.bufferSize, (bufferData.size() * sizeof(glm::vec2)));
            return true;
        }

        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), SSBOData.bufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map SSBO memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, bufferData.data(), (bufferData.size() * sizeof(glm::vec2)));

        if (!SSBOData.isHostCoherent) {
            vmaFlushAllocation(engineDevice.allocator(), SSBOData.bufferAlloc, 0, SSBOData.bufferSize);
        }

        vmaUnmapMemory(engineDevice.allocator(), SSBOData.bufferAlloc);
        return true;
    }

    // NodeTransformData - persistent
    bool ShaderStorageBuffer::uploadPersistentSsboData(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, std::span<const NodeTransformData> bufferData)
    {

        assert(SSBOData.mapped != nullptr && "Persistent SSBO has no mapped memory range.");

        // This might be a silly invariant to keep up with
        if (bufferData.empty()) {
            return false;
        }

        if ((bufferData.size() * sizeof(NodeTransformData)) > SSBOData.bufferSize) {
            Logger::log(1, "%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, SSBOData.buffer, SSBOData.bufferSize, (bufferData.size() * sizeof(NodeTransformData)));
            return true;
        }

        std::memcpy(SSBOData.mapped, bufferData.data(), (bufferData.size() * sizeof(NodeTransformData)));

        if (!SSBOData.isHostCoherent) {
            vmaFlushAllocation(engineDevice.allocator(), SSBOData.bufferAlloc, 0, SSBOData.bufferSize);
        }

        return true;
    }

    // mat4 persistent
    bool ShaderStorageBuffer::uploadPersistentSsboData(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, std::span<const glm::mat4> bufferData)
    {

        assert(SSBOData.mapped != nullptr && "Persistent SSBO has no mapped memory range.");

        // This might be a silly invariant to keep up with
        if (bufferData.empty()) {
            return false;
        }

        if ((bufferData.size() * sizeof(glm::mat4)) > SSBOData.bufferSize) {
            Logger::log(1, "%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, SSBOData.buffer, SSBOData.bufferSize, (bufferData.size() * sizeof(glm::mat4)));
            return true;
        }

        std::memcpy(SSBOData.mapped, bufferData.data(), (bufferData.size() * sizeof(glm::mat4)));

        if (!SSBOData.isHostCoherent) {
            vmaFlushAllocation(engineDevice.allocator(), SSBOData.bufferAlloc, 0, SSBOData.bufferSize);
        }

        return true;
    }

    bool ShaderStorageBuffer::checkForResize(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, size_t bufferSize) {
        if (bufferSize > SSBOData.bufferSize) {
            Logger::log(1, "%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, SSBOData.buffer, SSBOData.bufferSize, bufferSize);
            return true;
        }
        return false;
    }

    void ShaderStorageBuffer::cleanup(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData) {
        VkResult result = vkQueueWaitIdle(engineDevice.graphicsQueue());
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s fatal error: could not wait for device idle (error: %i)\n", __FUNCTION__, result);
        }
        vmaDestroyBuffer(engineDevice.allocator(), SSBOData.buffer, SSBOData.bufferAlloc);
    }
}