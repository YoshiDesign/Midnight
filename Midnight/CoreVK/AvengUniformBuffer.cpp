#include "CoreVK/AvengUniformBuffer.h"
#include "Utils/Logger.h"

namespace aveng {

    bool UniformBuffer::init(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkDeviceSize size) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo vmaAllocInfo{};
        vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        VkResult result = vmaCreateBuffer(engineDevice.allocator(), &bufferInfo, &vmaAllocInfo, &uboData.buffer, &uboData.bufferAlloc, nullptr);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not allocate uniform buffer via VMA (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        return true;
    }

    void UniformBuffer::uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices) {
        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), uboData.bufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map uniform buffer memory (error: %i)\n", __FUNCTION__, result);
            return;
        }
        std::memcpy(data, &matrices, sizeof(VkUploadMatrices));
        vmaUnmapMemory(engineDevice.allocator(), uboData.bufferAlloc);
        vmaFlushAllocation(engineDevice.allocator(), uboData.bufferAlloc, 0, uboData.bufferSize);

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
        vmaFlushAllocation(engineDevice.allocator(), uboData.bufferAlloc, 0, uboData.bufferSize);

    }

    void UniformBuffer::cleanup(EngineDevice& engineDevice, VkUniformBufferData& uboData) {
        vmaDestroyBuffer(engineDevice.allocator(), uboData.buffer, uboData.bufferAlloc);
    }


}