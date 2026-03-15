#pragma once

#include <span>
#include <memory>
#include <vulkan/vulkan.h>
#include "CoreVK/VkRenderData.h"
#include "CoreVK/aveng_buffer.h"
#include "Utils/Logger.h"

namespace aveng {
    class UniformBuffer{
      public:
        static bool init(EngineDevice& engineDevice, VkUniformBufferData & uboData, VkDeviceSize size, MapMode mode);

        /* TODO - Templating */

        /* Note the unique data for each signature - these uploads are specific to the data they cater to */

        template <typename T>
        static bool uploadUboDataRange(
            EngineDevice& engineDevice,
            VkUniformBufferData& uboData,
            const std::span<const T> bufferData,
            uint32_t offsetElements)
        {
            if (bufferData.empty()) {
                return true;
            }

            const VkDeviceSize writeOffsetBytes =
                static_cast<VkDeviceSize>(offsetElements) * sizeof(T);
            const VkDeviceSize writeSizeBytes =
                static_cast<VkDeviceSize>(bufferData.size()) * sizeof(T);

            if ((writeOffsetBytes + writeSizeBytes) > uboData.bufferSize) {
                Logger::log(
                    1,
                    "%s: UBO range out of bounds. Buffer %p, bufferSize=%zu, writeOffset=%zu, writeSize=%zu\n",
                    __FUNCTION__,
                    uboData.buffer,
                    static_cast<size_t>(uboData.bufferSize),
                    static_cast<size_t>(writeOffsetBytes),
                    static_cast<size_t>(writeSizeBytes)
                );
                return false;
            }

            void* mapped = nullptr;

            // Use persistent mapping if available
            if (uboData.mapped) {
                mapped = uboData.mapped;
            }
            else {
                VkResult result = vmaMapMemory(engineDevice.allocator(), uboData.bufferAlloc, &mapped);
                if (result != VK_SUCCESS) {
                    Logger::log(1, "%s error: could not map UBO memory (error: %i)\n", __FUNCTION__, result);
                    return false;
                }
            }

            auto* dst = static_cast<std::byte*>(mapped) + writeOffsetBytes;
            std::memcpy(dst, bufferData.data(), static_cast<size_t>(writeSizeBytes));

            if (!uboData.isHostCoherent) {
                vmaFlushAllocation(
                    engineDevice.allocator(),
                    uboData.bufferAlloc,
                    writeOffsetBytes,
                    writeSizeBytes
                );
            }

            if (!uboData.mapped) {
                vmaUnmapMemory(engineDevice.allocator(), uboData.bufferAlloc);
            }

            return true;
        }

        /* UNUSED */ static void uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices);
        /* UNUSED */ static void uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, PointLightData pointLightData);

        static void uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, std::span<ModelSkinMeta> skinMeta);

        static void uploadPersistentData(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices);
        static void uploadPersistentData(EngineDevice& engineDevice, VkUniformBufferData& uboData, PointLightData pointLightData);

        static void cleanup(EngineDevice& engineDevice, VkUniformBufferData& uboData);
    };
}
