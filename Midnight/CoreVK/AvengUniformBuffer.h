#pragma once

#include <vulkan/vulkan.h>

#include "VkRenderData.h"
namespace aveng {
    class UniformBuffer {
    public:
        static bool init(VkRenderData& renderData, EngineDevice& engineDevice, VkUniformBufferData& uboData);
        static void uploadData(VkRenderData& renderData, EngineDevice& engineDevice, VkUniformBufferData& uboData, VkUploadMatrices matrices);
        static void cleanup(VkRenderData& renderData, EngineDevice& engineDevice, VkUniformBufferData& uboData);
    };

}
