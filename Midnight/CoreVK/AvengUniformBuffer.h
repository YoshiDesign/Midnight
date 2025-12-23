#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include "CoreVK/VkRenderData.h"
#include "CoreVK/aveng_buffer.h"

namespace aveng {
    class UniformBuffer{
      public:
        static bool init(EngineDevice& engineDevice, VkUniformBufferData & uboData, VkDeviceSize size, MapMode mode);

        /* TODO - Templating */

        /* Note the unique data for each signature - these uploads are specific to the data they cater to */
        /* UNUSED */ static void uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices);
        /* UNUSED */ static void uploadData(EngineDevice& engineDevice, VkUniformBufferData& uboData, PointLightData pointLightData);
        static void uploadPersistentData(EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices);
        static void uploadPersistentData(EngineDevice& engineDevice, VkUniformBufferData& uboData, PointLightData pointLightData);

        static void cleanup(EngineDevice& engineDevice, VkUniformBufferData& uboData);
    };
}
