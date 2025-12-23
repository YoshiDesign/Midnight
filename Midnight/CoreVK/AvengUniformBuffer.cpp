#include "CoreVK/AvengUniformBuffer.h"
#include "Utils/Logger.h"
#include "avpch.h"

namespace aveng {

    /*
    * Keep in mind when persistently mapping buffers:
    * -there’s a long-lived CPU pointer into GPU memory
    * -accidental writes are easier (bugs are nastier)
    * -you need to ensure no one writes outside the allocation / after destroy
    * -some platforms have higher mapped memory overhead than others. Even if supported, it could become a foot-gun
    * Rule: persistent mapping is best when the ownership and write paths are very clear
    * If you're multithreading with persistent memory maps, do your homework!
    * (per-frame UBOs, per-frame staging buffers, etc.).
    */

    bool UniformBuffer::init(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkDeviceSize size, MapMode mode) {

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo vmaAllocInfo{};
        vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        if (mode == MapMode::Persistent) {
            /* Warning */
            // When using VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT never access the buffer randomly e.g.pMappedData[i] 
            // Prepare your data in a local variable and memcpy() it to the mapped pointer all at once.
            vmaAllocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }

        VmaAllocationInfo readInfo{};

        VkResult result = vmaCreateBuffer(
            engineDevice.allocator(), 
            &bufferInfo, 
            &vmaAllocInfo, 
            &uboData.buffer, 
            &uboData.bufferAlloc, 
            &readInfo);

        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not allocate uniform buffer via VMA (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        if (mode == MapMode::Persistent) {
            uboData.mapped = readInfo.pMappedData;
        }

        // If the buffer turns out to be host coherent, you don't need to flush it after data is uploaded
        VkMemoryPropertyFlags memFlags{};
        vmaGetAllocationMemoryProperties(engineDevice.allocator(), uboData.bufferAlloc, &memFlags);
        uboData.isHostCoherent = (memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

        return true;
    }

    /* TODO - Make these templated functions */
    void UniformBuffer::uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices) {
        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), uboData.bufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map uniform buffer memory (error: %i)\n", __FUNCTION__, result);
            return;
        }
        std::memcpy(data, &matrices, sizeof(VkUploadMatrices));
        vmaUnmapMemory(engineDevice.allocator(), uboData.bufferAlloc);
        if (!uboData.isHostCoherent) {
            vmaFlushAllocation(engineDevice.allocator(), uboData.bufferAlloc, 0, uboData.bufferSize);
        }

    }

    void UniformBuffer::uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, PointLightData pointLightData) {

        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), uboData.bufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map uniform buffer memory (error: %i)\n", __FUNCTION__, result);
            return;
        }
        std::memcpy(data, &pointLightData, sizeof(PointLightData));
        vmaUnmapMemory(engineDevice.allocator(), uboData.bufferAlloc);

        if (!uboData.isHostCoherent) {
            vmaFlushAllocation(engineDevice.allocator(), uboData.bufferAlloc, 0, uboData.bufferSize);
        }

    }

    /* TODO - Make these templated functions */
    void UniformBuffer::uploadPersistentData(EngineDevice& engineDevice, VkUniformBufferData& uboData, PointLightData pointLightData) {

        std::memcpy(uboData.mapped, &pointLightData, sizeof(PointLightData));

        if (!uboData.isHostCoherent) {
            vmaFlushAllocation(engineDevice.allocator(), uboData.bufferAlloc, 0, uboData.bufferSize);
        }

    }

    void UniformBuffer::uploadPersistentData(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices) {

        std::memcpy(uboData.mapped, &matrices, sizeof(VkUploadMatrices));

        if (!uboData.isHostCoherent) {
            vmaFlushAllocation(engineDevice.allocator(), uboData.bufferAlloc, 0, uboData.bufferSize);
        }

    }

    void UniformBuffer::cleanup(EngineDevice& engineDevice, VkUniformBufferData& uboData) {
        vmaDestroyBuffer(engineDevice.allocator(), uboData.buffer, uboData.bufferAlloc);
        uboData.buffer = VK_NULL_HANDLE;
        uboData.bufferAlloc = VK_NULL_HANDLE;
        uboData.mapped = nullptr;
    }


}