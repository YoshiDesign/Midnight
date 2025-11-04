#pragma once

#include "../CoreVK/EngineDevice.h"

namespace aveng {

    /*
    * @class AvengBuffer
    * This class can be used to generate staging, index, uniform and storage buffers.
    * Now supports both traditional Vulkan memory allocation and VMA allocation.
    */
    class AvengBuffer {
    public:
        // Traditional constructor (existing - backward compatible)
        AvengBuffer(
            EngineDevice& device,
            VkDeviceSize instanceSize,
            uint32_t instanceCount,
            VkBufferUsageFlags usageFlags,
            VkMemoryPropertyFlags memoryPropertyFlags,
            VkDeviceSize minOffsetAlignment = 1
        );

        // New VMA constructor
        AvengBuffer(
            EngineDevice& device,
            VkDeviceSize instanceSize,
            uint32_t instanceCount,
            VkBufferUsageFlags usageFlags,
            VmaMemoryUsage memoryUsage,
            VkDeviceSize minOffsetAlignment = 1,
            VmaAllocationCreateFlags vmaFlags = 0
        );

        ~AvengBuffer();

        AvengBuffer(const AvengBuffer&) = delete;
        AvengBuffer& operator=(const AvengBuffer&) = delete;

        VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
        void unmap();

        void writeToBuffer(const void* data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
        VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
        VkDescriptorBufferInfo descriptorInfo(VkDeviceSize range = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
        VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

        // Indexes help wrap multiple instances into a single buffer
        void writeToIndex(const void* data, int index);
        VkResult flushIndex(int index);
        VkDescriptorBufferInfo descriptorInfoForIndex(int index);
        VkResult invalidateIndex(int index);

        VkBuffer getBuffer() const { return buffer; }
        void* getMappedMemory() const { return mapped; }
        uint32_t getInstanceCount() const { return instanceCount; }
        VkDeviceSize getInstanceSize() const { return instanceSize; }
        VkDeviceSize getAlignmentSize() const { return alignmentSize; }
        VkBufferUsageFlags getUsageFlags() const { return usageFlags; }
        VkDeviceSize getBufferSize() const { return bufferSize; }

        template <typename T>
        static bool uploadSsboData(AvengBuffer& Ssbo, std::vector<T> bufferData) {
            if (bufferData.empty()) {
                return false;
            }

            bool bufferResized = false;
            size_t bufferSize = bufferData.size() * sizeof(T);
            if (bufferSize > Ssbo.bufferSize) {
                std::printf("%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, Ssbo.buffer, Ssbo.bufferSize, bufferSize);
                // cleanup(renderData, SSBOData);
                // init(renderData, SSBOData, bufferSize); // THIS IS THE STORAGE BUFFER'S INIT FUNCTION WE HAVEN'T IMPLEMENTED OUR FLAVOR OF THIS YET
                bufferResized = true;
            }

            if (Ssbo.map() != VK_SUCCESS) {
                std::printf("%s error: could not map SSBO memory (error: %i)\n", __FUNCTION__, result);
                return false;
            }

            Ssbo.writeToBuffer();
            Ssbo.unmap();
            Ssbo.flush();

            return bufferResized;
        }

        static bool checkForResize(AvengBuffer& Ssbo, size_t bufferSize);
        
        // New getters for VMA support
        bool isUsingVMA() const { return usingVMA; }
        VmaAllocation getVmaAllocation() const { return vmaAllocation; }
        VkMemoryPropertyFlags getMemoryPropertyFlags() const { return memoryPropertyFlags; }

    private:

        // Instances (objects within a) of a (probably just dynamic-) uniform block must be at an offset that is an integer multiple of the minimum uniform buffer offset alignment device-value
        // This function returns the smallest size in bites that satisfies this requirement
        // So if our uniform buffer is 19bytes, but our device's min uniform buffer offset is 16bytes, we'll need 32bytes.
        // Note that vertex and index buffer's don't have an alignment requirement like storage and uniform buffers do.
        static VkDeviceSize getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment);

        EngineDevice& engineDevice;
        void* mapped = nullptr;
        VkBuffer buffer = VK_NULL_HANDLE;
        
        // Memory allocation - either traditional or VMA
        VkDeviceMemory memory = VK_NULL_HANDLE;      // Traditional allocation
        VmaAllocation vmaAllocation = VK_NULL_HANDLE; // VMA allocation
        bool usingVMA = false;                        // Track which allocation method is used

        VkDeviceSize bufferSize;
        uint32_t instanceCount;
        VkDeviceSize instanceSize;
        VkDeviceSize alignmentSize;
        VkBufferUsageFlags usageFlags;
        VkMemoryPropertyFlags memoryPropertyFlags;
    };

}  // namespace aveng