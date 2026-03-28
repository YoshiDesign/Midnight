#include "VertexBuffer.h"
#include <cstring>
#include <vulkan/vulkan.h>
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"
#include "Utils/Logger.h"

namespace aveng {

    bool VertexBuffer::init(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, unsigned int bufferSize) {
        /* avoid errors causes by zero buffer size */
        if (bufferSize == 0) {
            bufferSize = 1024;
        }

        /* vertex buffer */
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo bufferAllocInfo{};
        bufferAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        VkResult result = vmaCreateBuffer(engineDevice.allocator(), &bufferInfo, &bufferAllocInfo,
            &vertexBufferData.buffer, &vertexBufferData.bufferAlloc, nullptr);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not allocate vertex buffer via VMA (error: %i)\n", __FUNCTION__, result);
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
            &vertexBufferData.stagingBuffer, &vertexBufferData.stagingBufferAlloc, nullptr);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not allocate vertex staging buffer via VMA (error: %i)\n",
                __FUNCTION__, result);
            return false;
        }
        vertexBufferData.bufferSize = bufferSize;
        return true;
    }

    /*
    * VkLineVertex VBO
    */
    bool VertexBuffer::uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, const VkLineMesh& vertexData) {
        size_t vertexDataSize = vertexData.vertices.size() * sizeof(VkLineVertex);

        /* buffer too small, resize */
        if (vertexBufferData.bufferSize < vertexDataSize) {
            cleanup(engineDevice, vertexBufferData);

            if (!init(engineDevice, vertexBufferData, vertexDataSize)) {
                Logger::log(1, "%s error: could not create vertex buffer of size %i bytes\n",
                    __FUNCTION__, vertexDataSize);
                return false;
            }
            Logger::log(1, "%s: vertex buffer resize to %i bytes\n", __FUNCTION__, vertexDataSize);
            vertexBufferData.bufferSize = vertexDataSize;
        }

        /* copy data to staging buffer */
        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, vertexData.vertices.data(), vertexDataSize);
        vmaUnmapMemory(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc);
        vmaFlushAllocation(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc, 0, vertexDataSize);

        /* trigger upload */
        return uploadToGPU(engineDevice, vertexBufferData);
    
    }

    bool VertexBuffer::uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, const VkMesh& vertexData) {

        unsigned int vertexDataSize = vertexData.vertices.size() * sizeof(VkVertex);

        /* buffer too small, resize */
        if (vertexBufferData.bufferSize < vertexDataSize) {
            cleanup(engineDevice, vertexBufferData);

            if (!init(engineDevice, vertexBufferData, vertexDataSize)) {
                Logger::log(1, "%s error: could not create vertex buffer of size %i bytes\n",
                    __FUNCTION__, vertexDataSize);
                return false;
            }
            Logger::log(1, "%s: vertex buffer resize to %i bytes\n", __FUNCTION__, vertexDataSize);
            vertexBufferData.bufferSize = vertexDataSize;
        }

        /* copy data to staging buffer */
        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, vertexData.vertices.data(), vertexDataSize);
        vmaUnmapMemory(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc);
        vmaFlushAllocation(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc, 0, vertexDataSize);

        return uploadToGPU(engineDevice, vertexBufferData);

        return true;
    }

    bool VertexBuffer::uploadData(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, const std::vector<glm::vec3>& vertexData) {
        unsigned int vertexDataSize = vertexData.size() * sizeof(glm::vec3);

        /* buffer too small, resize */
        if (vertexBufferData.bufferSize < vertexDataSize) {
            cleanup(engineDevice, vertexBufferData);

            if (!init(engineDevice, vertexBufferData, vertexDataSize)) {
                Logger::log(1, "%s error: could not create vertex buffer of size %i bytes\n",
                    __FUNCTION__, vertexDataSize);
                return false;
            }
            Logger::log(1, "%s: vertex buffer resize to %i bytes\n", __FUNCTION__, vertexDataSize);
            vertexBufferData.bufferSize = vertexDataSize;
        }

        /* copy data to staging buffer */
        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, vertexData.data(), vertexDataSize);
        vmaUnmapMemory(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc);
        vmaFlushAllocation(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc, 0, vertexDataSize);

        return uploadToGPU(engineDevice, vertexBufferData);

        return true;
    }

    bool VertexBuffer::uploadToGPU(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData) {
        VkBufferMemoryBarrier vertexBufferBarrier{};
        vertexBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        vertexBufferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vertexBufferBarrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        vertexBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vertexBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vertexBufferBarrier.buffer = vertexBufferData.stagingBuffer;
        vertexBufferBarrier.offset = 0;
        vertexBufferBarrier.size = vertexBufferData.bufferSize;

        VkBufferCopy stagingBufferCopy{};
        stagingBufferCopy.srcOffset = 0;
        stagingBufferCopy.dstOffset = 0;
        stagingBufferCopy.size = vertexBufferData.bufferSize;

        /* trigger data transfer via command buffer */
        VkCommandBuffer commandBuffer = engineDevice.createSingleShotBuffer();;

        vkCmdCopyBuffer(commandBuffer, vertexBufferData.stagingBuffer,
            vertexBufferData.buffer, 1, &stagingBufferCopy);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1, &vertexBufferBarrier, 0, nullptr);

        if (!engineDevice.submitSingleShotBuffer(commandBuffer)) {
            return false;
        }

        return true;
    }

    void VertexBuffer::cleanup(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData) {
        if (vertexBufferData.stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engineDevice.allocator(), vertexBufferData.stagingBuffer, vertexBufferData.stagingBufferAlloc);
            vertexBufferData.stagingBuffer = VK_NULL_HANDLE;
            vertexBufferData.stagingBufferAlloc = VK_NULL_HANDLE;
        }
        if (vertexBufferData.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engineDevice.allocator(), vertexBufferData.buffer, vertexBufferData.bufferAlloc);
            vertexBufferData.buffer = VK_NULL_HANDLE;
            vertexBufferData.bufferAlloc = VK_NULL_HANDLE;
        }
    }

    bool VertexBuffer::fillStaging(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData, const std::vector<glm::vec3>& vertexData) {
        size_t vertexDataSize = vertexData.size() * sizeof(glm::vec3);
        void* data;
        VkResult result = vmaMapMemory(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc, &data);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not map memory (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        std::memcpy(data, vertexData.data(), vertexDataSize);
        vmaUnmapMemory(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc);
        vmaFlushAllocation(engineDevice.allocator(), vertexBufferData.stagingBufferAlloc, 0, vertexDataSize);
        return true;
    }

    void VertexBuffer::recordCopy(VkCommandBuffer cmd, VkVertexBufferData& vertexBufferData) {
        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = vertexBufferData.bufferSize;
        vkCmdCopyBuffer(cmd, vertexBufferData.stagingBuffer, vertexBufferData.buffer, 1, &copyRegion);
    }

    void VertexBuffer::cleanupStaging(EngineDevice& engineDevice, VkVertexBufferData& vertexBufferData) {
        if (vertexBufferData.stagingBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(engineDevice.allocator(), vertexBufferData.stagingBuffer, vertexBufferData.stagingBufferAlloc);
            vertexBufferData.stagingBuffer = VK_NULL_HANDLE;
            vertexBufferData.stagingBufferAlloc = VK_NULL_HANDLE;
        }
    }

}