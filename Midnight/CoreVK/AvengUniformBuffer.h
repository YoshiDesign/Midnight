#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include "CoreVK/VkRenderData.h"
#include "CoreVK/aveng_buffer.h"

namespace aveng {
    class UniformBuffer{
      public:
        static bool init(EngineDevice& engineDevice, VkUniformBufferData & uboData);
        static void uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices);
        static void cleanup(EngineDevice& engineDevice, VkUniformBufferData& uboData);
    };
}
