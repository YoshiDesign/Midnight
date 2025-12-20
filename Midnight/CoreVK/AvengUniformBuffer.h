#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include "CoreVK/VkRenderData.h"
#include "CoreVK/aveng_buffer.h"

namespace aveng {
    class UniformBuffer{
      public:
        static bool init(EngineDevice& engineDevice, VkUniformBufferData & uboData, VkDeviceSize size);

        /* TODO - We don't need multiple sig's, make these polymorphic */
        static void uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices);
        static void uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, PointLightData pointLightData);

        static void cleanup(EngineDevice& engineDevice, VkUniformBufferData& uboData);
    };
}
