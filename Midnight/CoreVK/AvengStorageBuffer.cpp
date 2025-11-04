#include "AvengStorageBuffer.h"
#include "CoreVK/EngineDevice.h"
#include <cstdio>
namespace aveng {
    // TODO - Remove engine device from sig. Just pass in what you need, it's too big
    bool ShaderStorageBuffer::init(VkRenderData& renderData, EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, size_t bufferSize) {
        /* avoid errors with zero sized buffers */
        if (bufferSize == 0) {
            bufferSize = 1024;
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        VmaAllocationCreateInfo vmaAllocInfo{};
        vmaAllocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        VkResult result = vmaCreateBuffer(engineDevice.allocator(), &bufferInfo, &vmaAllocInfo,
            &SSBOData.buffer, &SSBOData.bufferAlloc, nullptr);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not allocate SSBO via VMA (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        SSBOData.bufferSize = bufferSize;
        std::printf("%s: created SSBO of size %i\n", __FUNCTION__, bufferSize);
        return true;
    }

    // TODO
    bool ShaderStorageBuffer::checkForResize(VkRenderData& renderData, EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, size_t bufferSize) {
        if (bufferSize > SSBOData.bufferSize) {
            std::printf("%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, SSBOData.buffer, SSBOData.bufferSize, bufferSize);
            cleanup(renderData, engineDevice, SSBOData);
            init(renderData, engineDevice, SSBOData, bufferSize);
            return true;
        }
        return false;
    }

    void ShaderStorageBuffer::cleanup(VkRenderData& renderData, EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData) {
        VkResult result = vkQueueWaitIdle(engineDevice.graphicsQueue());
        if (result != VK_SUCCESS) {
            std::printf("%s fatal error: could not wait for device idle (error: %i)\n", __FUNCTION__, result);
        }
        vmaDestroyBuffer(engineDevice.allocator(), SSBOData.buffer, SSBOData.bufferAlloc);
    }

}