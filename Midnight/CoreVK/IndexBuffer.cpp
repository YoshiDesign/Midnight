#include <cstring>

#include "IndexBuffer.h"

namespace aveng {
    bool IndexBuffer::init(EngineDevice& engineDevice, VkIndexBufferData& bufferData,
        size_t bufferSize) {
        /* index buffer */
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo bufferAllocInfo{};
        bufferAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkResult result = vmaCreateBuffer(engineDevice.allocator(), &bufferInfo, &bufferAllocInfo,
            &bufferData.buffer, &bufferData.bufferAlloc, nullptr);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not allocate index buffer via VMA (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        /* staging buffer for copy */
        VkBufferCreateInfo stagingBufferInfo{};
        stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufferInfo.size = bufferSize;;
        stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        result = vmaCreateBuffer(engineDevice.allocator(), &stagingBufferInfo, &stagingAllocInfo,
            &bufferData.stagingBuffer, &bufferData.stagingBufferAlloc, nullptr);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not allocate index staging buffer via VMA (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        bufferData.bufferSize = bufferSize;
        return true;
    }

    bool IndexBuffer::uploadData(EngineDevice& engineDevice, VkIndexBufferData& bufferData, VkMesh vertexData) {
        /* buffer too small, resize */
        unsigned int indexDataSize = vertexData.indices.size() * sizeof(uint32_t);
        if (bufferData.bufferSize < indexDataSize) {
            cleanup(engineDevice, bufferData);

            if (!init(engineDevice, bufferData, indexDataSize)) {
                std::printf("%s error: could not create index buffer of size %i bytes\n", __FUNCTION__, indexDataSize);
                return false;
            }
            std::printf("%s: index buffer resize to %i bytes\n", __FUNCTION__, indexDataSize);
            // bufferData.bufferSize = indexDataSize;
        }

        /* copy data to staging buffer */
        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), bufferData.stagingBufferAlloc, &data);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not map index buffer memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, vertexData.indices.data(), indexDataSize);
        vmaUnmapMemory(engineDevice.allocator(), bufferData.stagingBufferAlloc);
        vmaFlushAllocation(engineDevice.allocator(), bufferData.stagingBufferAlloc, 0, indexDataSize);

        VkBufferMemoryBarrier indexBufferBarrier{};
        indexBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        indexBufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // VK_ACCESS_MEMORY_WRITE_BIT; // The latter here assumes we want read-access after copying
        indexBufferBarrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
        indexBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        indexBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        indexBufferBarrier.buffer = bufferData.stagingBuffer;
        indexBufferBarrier.offset = 0;
        indexBufferBarrier.size = bufferData.bufferSize;

        VkBufferCopy stagingBufferCopy{};
        stagingBufferCopy.srcOffset = 0;
        stagingBufferCopy.dstOffset = 0;
        stagingBufferCopy.size = bufferData.bufferSize;

        /* trigger data transfer via command buffer */
        VkCommandBuffer commandBuffer = engineDevice.createSingleShotBuffer();

        vkCmdCopyBuffer(commandBuffer, bufferData.stagingBuffer,
            bufferData.buffer, 1, &stagingBufferCopy);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &indexBufferBarrier, 0, nullptr);

        if (!engineDevice.submitSingleShotBuffer(commandBuffer)) {
            return false;
        }

        return true;
    }
    
    bool IndexBuffer::uploadData(EngineDevice& engineDevice, VkIndexBufferData& bufferData, std::vector<uint32_t>& indices) {
        /* buffer too small, resize */
        unsigned int indexDataSize = indices.size() * sizeof(uint32_t);
        if (bufferData.bufferSize < indexDataSize) {
            cleanup(engineDevice, bufferData);

            if (!init(engineDevice, bufferData, indexDataSize)) {
                std::printf("%s error: could not create index buffer of size %i bytes\n", __FUNCTION__, indexDataSize);
                return false;
            }
            std::printf("%s: index buffer resize to %i bytes\n", __FUNCTION__, indexDataSize);
            // bufferData.bufferSize = indexDataSize;
        }

        /* copy data to staging buffer */
        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), bufferData.stagingBufferAlloc, &data);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not map index buffer memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, indices.data(), indexDataSize);
        vmaUnmapMemory(engineDevice.allocator(), bufferData.stagingBufferAlloc);
        vmaFlushAllocation(engineDevice.allocator(), bufferData.stagingBufferAlloc, 0, indexDataSize);

        VkBufferMemoryBarrier indexBufferBarrier{};
        indexBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        indexBufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // VK_ACCESS_MEMORY_WRITE_BIT; // The latter here assumes we want read-access after copying
        indexBufferBarrier.dstAccessMask = VK_ACCESS_INDEX_READ_BIT;
        indexBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        indexBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        indexBufferBarrier.buffer = bufferData.stagingBuffer;
        indexBufferBarrier.offset = 0;
        indexBufferBarrier.size = bufferData.bufferSize;

        VkBufferCopy stagingBufferCopy{};
        stagingBufferCopy.srcOffset = 0;
        stagingBufferCopy.dstOffset = 0;
        stagingBufferCopy.size = bufferData.bufferSize;

        /* trigger data transfer via command buffer */
        VkCommandBuffer commandBuffer = engineDevice.createSingleShotBuffer();

        vkCmdCopyBuffer(commandBuffer, bufferData.stagingBuffer,
            bufferData.buffer, 1, &stagingBufferCopy);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &indexBufferBarrier, 0, nullptr);

        if (!engineDevice.submitSingleShotBuffer(commandBuffer)) {
            return false;
        }

        return true;
    }

    void IndexBuffer::cleanup(EngineDevice& engineDevice, VkIndexBufferData& bufferData) {
        if (bufferData.stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engineDevice.allocator(), bufferData.stagingBuffer, bufferData.stagingBufferAlloc);
            bufferData.stagingBuffer = VK_NULL_HANDLE;
            bufferData.stagingBufferAlloc = nullptr;
        }
        if (bufferData.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engineDevice.allocator(), bufferData.buffer, bufferData.bufferAlloc);
            bufferData.buffer = VK_NULL_HANDLE;
            bufferData.bufferAlloc = nullptr;
        }
    }

    bool IndexBuffer::fillStaging(EngineDevice& engineDevice, VkIndexBufferData& bufferData, std::vector<uint32_t>& indices) {
        size_t indexDataSize = indices.size() * sizeof(uint32_t);
        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), bufferData.stagingBufferAlloc, &data);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not map index buffer memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, indices.data(), indexDataSize);
        vmaUnmapMemory(engineDevice.allocator(), bufferData.stagingBufferAlloc);
        vmaFlushAllocation(engineDevice.allocator(), bufferData.stagingBufferAlloc, 0, indexDataSize);
        return true;
    }

    void IndexBuffer::recordCopy(VkCommandBuffer cmd, VkIndexBufferData& bufferData) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = bufferData.bufferSize;
        vkCmdCopyBuffer(cmd, bufferData.stagingBuffer, bufferData.buffer, 1, &copyRegion);
    }

    void IndexBuffer::cleanupStaging(EngineDevice& engineDevice, VkIndexBufferData& bufferData) {
        if (bufferData.stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engineDevice.allocator(), bufferData.stagingBuffer, bufferData.stagingBufferAlloc);
            bufferData.stagingBuffer = VK_NULL_HANDLE;
            bufferData.stagingBufferAlloc = nullptr;
        }
    }

}