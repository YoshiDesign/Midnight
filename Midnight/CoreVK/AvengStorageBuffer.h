#pragma once

#include <vulkan/vulkan.h>
#include "Utils/glm_includes.h"
#include "Utils/Logger.h"
#include "CoreVK/VkRenderData.h"
#include "CoreVK/EngineDevice.h"
#include "CoreVK/aveng_buffer.h"
#include "avpch.h"


namespace aveng {
    class ShaderStorageBuffer {
    public:
        /* set an arbitraty buffer size as default */
        static bool init(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, MapMode mode, ResidentMode resMode=ResidentMode::CPU, size_t bufferSize=1024);

        template <typename T>
        static bool uploadSsboData(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, std::vector<T> bufferData) {
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
            vmaUnmapMemory(engineDevice.allocator(), SSBOData.bufferAlloc);

            if (!SSBOData.isHostCoherent) {
                vmaFlushAllocation(engineDevice.allocator(), SSBOData.bufferAlloc, 0, SSBOData.bufferSize);
            }

            return false;
        }
        template <typename T>
        static bool uploadPersistentSsboData(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData, std::vector<T> bufferData) {

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

            return false;
        }
        
        static bool checkForResize(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData,
            size_t bufferSize);

        static void cleanup(EngineDevice& engineDevice, VkShaderStorageBufferData& SSBOData);
    };
}