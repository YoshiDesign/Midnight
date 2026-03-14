#pragma once
#include <cstddef>
#include <span>
#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <AMD/vk_mem_alloc.h>
#include "Utils/glm_includes.h"
#include "Utils/Logger.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/VkRenderData.h"

/* 
 * Things to consider/study: 
 *   - staging uploads into device-local buffers should writes become more frequent (which they already do!)
 */

namespace aveng {

    struct MnMaterial;
    struct MnMaterialExt;

    class ShaderStorageBuffer {
    public:
        /* set an arbitraty buffer size as default */
        static bool init(
            EngineDevice& engineDevice, 
            VkShaderStorageBufferData& SSBOData, 
            MapMode mode, 
            ResidentMode resMode=ResidentMode::CPU, 
            size_t bufferSize=5120
        );

        template <typename T>
        static bool uploadSsboData(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, std::span<const T> bufferData) {
            if (bufferData.empty()) {
                return false;
            }

            if ((bufferData.size() * sizeof(T)) > SSBOData.bufferSize) {
                Logger::log(1, "%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, SSBOData.buffer, SSBOData.bufferSize, (bufferData.size() * sizeof(T)));
                return true;
            }

            void* data;
            VkResult result = vmaMapMemory(engineDevice.allocator(), SSBOData.bufferAlloc, &data);
            if (result != VK_SUCCESS) {
                Logger::log(1, "%s error: could not map SSBO memory (error: %i)\n", __FUNCTION__, result);
                return false;
            }
            std::memcpy(data, bufferData.data(), (bufferData.size() * sizeof(T)));

            if (!SSBOData.isHostCoherent) {
                vmaFlushAllocation(engineDevice.allocator(), SSBOData.bufferAlloc, 0, SSBOData.bufferSize);
            }

            vmaUnmapMemory(engineDevice.allocator(), SSBOData.bufferAlloc);
            return true;
        }

        /* Specified signatures for hot uploads - prevent type deduction step */

        static bool uploadSsboData(
            EngineDevice& engineDevice,
            VkShaderStorageBufferData& SSBOData,
            std::span<const glm::mat4> bufferData);

        static bool uploadSsboData(
            EngineDevice& engineDevice,
            VkShaderStorageBufferData& SSBOData,
            std::span<const MnMaterialExt> bufferData);

        static bool uploadSsboData(
            EngineDevice& engineDevice,
            VkShaderStorageBufferData& SSBOData,
            std::span<const MnMaterial> bufferData);

        static bool uploadSsboData(
            EngineDevice& engineDevice,
            VkShaderStorageBufferData& SSBOData,
            std::span<const glm::vec2> bufferData);
        
        static bool uploadSsboData(
            EngineDevice& engineDevice,
            VkShaderStorageBufferData& SSBOData,
            std::span<const int32_t> bufferData);

        static bool uploadPersistentSsboData(
            EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData,
            std::span<const NodeTransformData> bufferData
        );

        static bool uploadPersistentSsboData(
            EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData,
            std::span<const glm::mat4> bufferData
        );

        template <typename T>
        static bool uploadSsboDataRange(
            EngineDevice& engineDevice,
            VkShaderStorageBufferData& ssboData,
            const std::span<const T> bufferData,
            uint32_t offsetElements)
        {
            if (bufferData.empty()) {
                return true;
            }

            const VkDeviceSize writeOffsetBytes = static_cast<VkDeviceSize>(offsetElements) * sizeof(T);
            const VkDeviceSize writeSizeBytes = static_cast<VkDeviceSize>(bufferData.size()) * sizeof(T);

            if ((writeOffsetBytes + writeSizeBytes) > ssboData.bufferSize) {
                Logger::log(
                    1,
                    "%s: SSBO range out of bounds. Buffer %p, bufferSize=%zu, writeOffset=%zu, writeSize=%zu\n",
                    __FUNCTION__,
                    ssboData.buffer,
                    static_cast<size_t>(ssboData.bufferSize),
                    static_cast<size_t>(writeOffsetBytes),
                    static_cast<size_t>(writeSizeBytes)
                );
                return false;
            }

            void* mapped = nullptr;
            VkResult result = vmaMapMemory(engineDevice.allocator(), ssboData.bufferAlloc, &mapped);
            if (result != VK_SUCCESS) {
                Logger::log(1, "%s error: could not map SSBO memory (error: %i)\n", __FUNCTION__, result);
                return false;
            }

            auto* dst = static_cast<std::byte*>(mapped) + writeOffsetBytes;
            std::memcpy(dst, bufferData.data(), static_cast<size_t>(writeSizeBytes));

            if (!ssboData.isHostCoherent) {
                vmaFlushAllocation(
                    engineDevice.allocator(),
                    ssboData.bufferAlloc,
                    writeOffsetBytes,
                    writeSizeBytes
                );
            }

            vmaUnmapMemory(engineDevice.allocator(), ssboData.bufferAlloc);
            return true;
        }

        // Persistent objects do not need to be unmapped
        template <typename T>
        static bool uploadPersistentSsboData(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, std::span<const T> bufferData) {

            assert(SSBOData.mapped != nullptr && "Persistent SSBO has no mapped memory range.");

            // This might be a silly invariant to keep up with
            if (bufferData.empty()) {
                return false;
            }

            if ((bufferData.size() * sizeof(T)) > SSBOData.bufferSize) {
                Logger::log(1, "%s: resize SSBO %p from %i to %i bytes\n", __FUNCTION__, SSBOData.buffer, SSBOData.bufferSize, (bufferData.size() * sizeof(T)));
                return true;
            }

            std::memcpy(SSBOData.mapped, bufferData.data(), (bufferData.size() * sizeof(T)));

            if (!SSBOData.isHostCoherent) {
                vmaFlushAllocation(engineDevice.allocator(), SSBOData.bufferAlloc, 0, SSBOData.bufferSize);
            }

            return true;
        }
        
        static bool checkForResize(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData,
            size_t bufferSize);

        static void cleanup(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData);
    };
}