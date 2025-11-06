#include "SyncObjects.h"

namespace aveng {
    bool SyncObjects::init(EngineDevice& engineDevice, VkRenderData& renderData) {
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        if (vkCreateSemaphore(engineDevice.device(), &semaphoreInfo, nullptr, &renderData.rdPresentSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(engineDevice.device(), &semaphoreInfo, nullptr, &renderData.rdRenderSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(engineDevice.device(), &semaphoreInfo, nullptr, &renderData.rdComputeSemaphore) != VK_SUCCESS ||
            vkCreateSemaphore(engineDevice.device(), &semaphoreInfo, nullptr, &renderData.rdGraphicSemaphore) != VK_SUCCESS ||
            vkCreateFence(engineDevice.device(), &fenceInfo, nullptr, &renderData.rdRenderFence) != VK_SUCCESS ||
            vkCreateFence(engineDevice.device(), &fenceInfo, nullptr, &renderData.rdComputeFence) != VK_SUCCESS) {
            std::printf("%s error: failed to init sync objects\n", __FUNCTION__);
            return false;
        }
        return true;
    }

    void SyncObjects::cleanup(EngineDevice& engineDevice, VkRenderData& renderData) {
        vkDestroySemaphore(engineDevice.device(), renderData.rdPresentSemaphore, nullptr);
        vkDestroySemaphore(engineDevice.device(), renderData.rdRenderSemaphore, nullptr);
        vkDestroySemaphore(engineDevice.device(), renderData.rdComputeSemaphore, nullptr);
        vkDestroySemaphore(engineDevice.device(), renderData.rdGraphicSemaphore, nullptr);
        vkDestroyFence(engineDevice.device(), renderData.rdRenderFence, nullptr);
        vkDestroyFence(engineDevice.device(), renderData.rdComputeFence, nullptr);
    }
}