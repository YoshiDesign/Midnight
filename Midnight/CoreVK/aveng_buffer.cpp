/*
 * Encapsulates a vulkan buffer
 *
 * Initially based off VulkanBuffer by Sascha Willems -
 * https://github.com/SaschaWillems/Vulkan/blob/master/base/VulkanBuffer.h
 * 
 * Notes:
 * 
 * Buffers represent linear arrays of data which are used for various purposes 
 * by binding them to a graphics or compute pipeline via descriptor sets or via 
 * certain commands, or by directly specifying them as parameters to certain commands.
 * 
 */

#include "aveng_buffer.h"

 // std
#include <cassert>
#include <cstring>
#include <iostream>

namespace aveng {

    /**
     * Returns the minimum instance size required to be compatible with devices minOffsetAlignment
     *
     * @param instanceSize The size of an instance
     * @param minOffsetAlignment The minimum required alignment, in bytes, for the offset member
     * (e.g. minUniformBufferOffsetAlignment)
     *
     * @return VkResult of the buffer mapping call
     */
    VkDeviceSize AvengBuffer::getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment) {
        if (minOffsetAlignment > 0) {
            return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
        }
        return instanceSize;
    }

    /*
     * Legacy Constructor - Calls to engineDevice.createBuffer (existing - backward compatible)
     */
    AvengBuffer::AvengBuffer(
        EngineDevice& device,
        VkDeviceSize instanceSize,
        uint32_t instanceCount,
        VkBufferUsageFlags usageFlags,
        VkMemoryPropertyFlags memoryPropertyFlags,
        VkDeviceSize minOffsetAlignment
    )
        : engineDevice{ device },
        instanceSize{ instanceSize },
        instanceCount{ instanceCount },
        usageFlags{ usageFlags },
        memoryPropertyFlags{ memoryPropertyFlags },
        usingVMA{ false }
    {

        alignmentSize = getAlignment(instanceSize, minOffsetAlignment);
        bufferSize = alignmentSize * instanceCount;

        // Call to engineDevice - traditional allocation
        device.createBuffer(bufferSize, usageFlags, memoryPropertyFlags, buffer, memory);
    }

    /*
    * VMA Constructor - Uses VMA for memory allocation
    */
    AvengBuffer::AvengBuffer(
        EngineDevice& device,
        VkDeviceSize instanceSize,
        uint32_t instanceCount,
        VkBufferUsageFlags usageFlags,
        VmaMemoryUsage memoryUsage,
        VkDeviceSize minOffsetAlignment,
        VmaAllocationCreateFlags vmaFlags
    )
        : engineDevice{ device },
        instanceSize{ instanceSize },
        instanceCount{ instanceCount },
        usageFlags{ usageFlags },
        memoryPropertyFlags{ 0 }, // Not used for VMA
        usingVMA{ true }
    {

        alignmentSize = getAlignment(instanceSize, minOffsetAlignment);
        bufferSize = alignmentSize * instanceCount;

        switch (usageFlags)
        {
        case VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT: std::cout << "Creating VMA Uniform Buffer\n" << std::endl;
        case VK_BUFFER_USAGE_STORAGE_BUFFER_BIT: std::cout << "Creating VMA Storage Buffer\n" << std::endl;
        default:
            std::cout << "Instance Size:\t" << instanceSize
                << "Instance Count:\t" << instanceCount
                << "Alignment Size:\t" << alignmentSize
                << "Buffer Size:\t" << bufferSize
            << std::endl;
            break;
        }

        // Call to engineDevice - VMA allocation
        device.createBufferWithVMA(bufferSize, usageFlags, memoryUsage, buffer, vmaAllocation, vmaFlags);
    }

    AvengBuffer::~AvengBuffer() 
    {
        std::cout << "Destroying AvengBuffer: " << usingVMA << std::endl;
        unmap();
        
        if (usingVMA) {
            // VMA cleanup
            vmaDestroyBuffer(engineDevice.allocator(), buffer, vmaAllocation);
        } else {
            // Traditional cleanup
            vkDestroyBuffer(engineDevice.device(), buffer, nullptr);
            vkFreeMemory(engineDevice.device(), memory, nullptr);
        }
    }

    /**
     * Map a memory range of this buffer. If successful, mapped points to the specified buffer range.
     *
     * @param size (Optional) Size of the memory range to map. Pass VK_WHOLE_SIZE to map the complete
     * buffer range.
     * @param offset (Optional) Byte offset from beginning
     *
     * @return VkResult of the buffer mapping call
     */
    VkResult AvengBuffer::map(VkDeviceSize size, VkDeviceSize offset) {
        assert(buffer && (memory || vmaAllocation) && "Called map on buffer before create");
        
        if (usingVMA) {
            std::cout << "Mapping with VMA Allocator" << std::endl;
            return vmaMapMemory(engineDevice.allocator(), vmaAllocation, &mapped);
        } else {
            return vkMapMemory(engineDevice.device(), memory, offset, size, 0, &mapped);
        }
    }

    /**
     * Unmap a mapped memory range
     *
     * @note Does not return a result as vkUnmapMemory can't fail
     */
    void AvengBuffer::unmap() 
    {
        if (mapped) {
            if (usingVMA) {
                vmaUnmapMemory(engineDevice.allocator(), vmaAllocation);
            } else {
                vkUnmapMemory(engineDevice.device(), memory);
            }
            mapped = nullptr;
        }
    }

    /**
     * Copies the specified data to the mapped buffer. Default value writes whole buffer range
     *
     * @param data Pointer to the data to copy
     * @param size (Optional) Size of the data to copy. Pass VK_WHOLE_SIZE to flush the complete buffer
     * range.
     * @param offset (Optional) Byte offset from beginning of mapped region
     *
     */
    void AvengBuffer::writeToBuffer(const void* data, VkDeviceSize size, VkDeviceSize offset) 
    {
        assert(mapped && "Cannot copy to unmapped buffer");

        if (size == VK_WHOLE_SIZE) {
            memcpy(mapped, data, bufferSize);
        }
        else {
            char* memOffset = (char*)mapped;
            memOffset += offset;
            memcpy(memOffset, data, size);
        }
    }

    /**
     * Flush a memory range of the buffer to make it visible to the device
     *
     * @note Only required for non-coherent memory
     *
     * @param size (Optional) Size of the memory range to flush. Pass VK_WHOLE_SIZE to flush the
     * complete buffer range.
     * @param offset (Optional) Byte offset from beginning
     *
     * @return VkResult of the flush call
     */
    VkResult AvengBuffer::flush(VkDeviceSize size, VkDeviceSize offset) 
    {
        if (usingVMA) {
            return vmaFlushAllocation(engineDevice.allocator(), vmaAllocation, offset, size);
        } else {
            VkMappedMemoryRange mappedRange = {};
            mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mappedRange.memory = memory;
            mappedRange.offset = offset;
            mappedRange.size = size;
            return vkFlushMappedMemoryRanges(engineDevice.device(), 1, &mappedRange);
        }
    }

    /**
     * Invalidate a memory range of the buffer to make it visible to the host
     *
     * @note Only required for non-coherent memory
     *
     * @param size (Optional) Size of the memory range to invalidate. Pass VK_WHOLE_SIZE to invalidate
     * the complete buffer range.
     * @param offset (Optional) Byte offset from beginning
     *
     * @return VkResult of the invalidate call
     */
    VkResult AvengBuffer::invalidate(VkDeviceSize size, VkDeviceSize offset) 
    {
        if (usingVMA) {
            return vmaInvalidateAllocation(engineDevice.allocator(), vmaAllocation, offset, size);
        } else {
            VkMappedMemoryRange mappedRange = {};
            mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            mappedRange.memory = memory;
            mappedRange.offset = offset;
            mappedRange.size = size;
            return vkInvalidateMappedMemoryRanges(engineDevice.device(), 1, &mappedRange);
        }
    }

    /**
     * Create a buffer info descriptor
     *
     * @param size (Optional) Size of the memory range of the descriptor
     * @param offset (Optional) Byte offset from beginning
     *
     * @return VkDescriptorBufferInfo of specified offset and range
     */
    VkDescriptorBufferInfo AvengBuffer::descriptorInfo(VkDeviceSize range, VkDeviceSize offset) 
    {
        return VkDescriptorBufferInfo {
            buffer,
            offset,
            range // VK_WHOLE_SIZE, typically?
        };
    }

    /**
     * Copies "instanceSize" bytes of data to the mapped buffer at an offset of index * alignmentSize
     *
     * @param data Pointer to the data to copy
     * @param index Used in offset calculation
     *
     */
    void AvengBuffer::writeToIndex(const void* data, int index) 
    {
        writeToBuffer(data, instanceSize, index * alignmentSize);
    }

    /**
     *  Flush the memory range at index * alignmentSize of the buffer to make it visible to the device
     *
     * @param index Used in offset calculation
     *
     */
    VkResult AvengBuffer::flushIndex(int index) { return flush(alignmentSize, index * alignmentSize); }

    /**
     * Create a buffer info descriptor
     *
     * @param index Specifies the region given by index * alignmentSize
     *
     * @return VkDescriptorBufferInfo for instance at index
     */
    VkDescriptorBufferInfo AvengBuffer::descriptorInfoForIndex(int index) 
    {
        return descriptorInfo(alignmentSize, index * alignmentSize);
    }

    /**
     * Invalidate a memory range of the buffer to make it visible to the host
     *
     * @note Only required for non-coherent memory
     *
     * @param index Specifies the region to invalidate: index * alignmentSize
     *
     * @return VkResult of the invalidate call
     */
    VkResult AvengBuffer::invalidateIndex(int index) 
    {
        return invalidate(alignmentSize, index * alignmentSize);
    }

    //void AvengBuffer::cleanup(VkRenderData& renderData, VkShaderStorageBufferData& SSBOData) {
    //    VkResult result = vkQueueWaitIdle(renderData.rdGraphicsQueue);
    //    if (result != VK_SUCCESS) {
    //        Logger::log(1, "%s fatal error: could not wait for device idle (error: %i)\n", __FUNCTION__, result);
    //    }
    //    vmaDestroyBuffer(renderData.rdAllocator, SSBOData.buffer, SSBOData.bufferAlloc);
    //}

}  // namespace aveng