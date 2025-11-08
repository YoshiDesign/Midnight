#pragma once
/* Vulkan sync objects */
#pragma once

#include <vulkan/vulkan.h>

#include "VkRenderData.h"
namespace aveng {
    class SyncObjects {
    public:
        static bool init(EngineDevice& engineDevice, VkRenderData& renderData, int frames);
        static void cleanup(EngineDevice& engineDevice, VkRenderData& renderData, int frames);
    };
}