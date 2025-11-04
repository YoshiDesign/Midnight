#pragma once

#include <vulkan/vulkan.h>
#include "CoreVK/VkRenderData.h"
#include "CoreVK/EngineDevice.h"

namespace aveng {
    class CommandBuffer {
    public:
        static bool init(EngineDevice& engineDevice, std::vector<VkCommandBuffer>& commandBuffers);

        static bool reset(VkCommandBuffer& commandBuffer, VkCommandBufferResetFlags flags = 0);
        static bool begin(VkCommandBuffer& commandBuffer, VkCommandBufferBeginInfo& beginInfo);
        static bool beginSingleShot(VkCommandBuffer& commandBuffer);
        static bool end(VkCommandBuffer& commandBuffer);

        static VkCommandBuffer createSingleShotBuffer(VkRenderData renderData, EngineDevice& engineDevice, VkCommandPool pool);
        static bool submitSingleShotBuffer(VkRenderData renderData, VkCommandPool pool, VkCommandBuffer commandBuffer, VkQueue queue);

        static void cleanup(VkRenderData renderData, VkCommandPool pool, VkCommandBuffer commandBuffer);
    };

}