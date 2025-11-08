#include "SyncObjects.h"

namespace aveng {
    bool SyncObjects::init(EngineDevice& engineDevice, VkRenderData& renderData, int frames) {

        for (int i = 0; i < frames; i++) {
            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            if (vkCreateSemaphore(engineDevice.device(), &semaphoreInfo, nullptr, &renderData.rdPresentSemaphore[i]) != VK_SUCCESS ||
                vkCreateSemaphore(engineDevice.device(), &semaphoreInfo, nullptr, &renderData.rdRenderSemaphore[i]) != VK_SUCCESS ||
                vkCreateSemaphore(engineDevice.device(), &semaphoreInfo, nullptr, &renderData.rdComputeSemaphore[i]) != VK_SUCCESS ||
                vkCreateSemaphore(engineDevice.device(), &semaphoreInfo, nullptr, &renderData.rdGraphicSemaphore[i]) != VK_SUCCESS ||
                vkCreateFence(engineDevice.device(), &fenceInfo, nullptr, &renderData.rdRenderFence[i]) != VK_SUCCESS ||
                vkCreateFence(engineDevice.device(), &fenceInfo, nullptr, &renderData.rdComputeFence[i]) != VK_SUCCESS) {
                std::printf("%s error: failed to init sync objects\n", __FUNCTION__);
                return false;
            }
        }

        return true;
    }

    void SyncObjects::cleanup(EngineDevice& engineDevice, VkRenderData& renderData, int frames) {
        for (int i = 0; i < frames; i++) {
            vkDestroySemaphore(engineDevice.device(), renderData.rdPresentSemaphore[i], nullptr);
            vkDestroySemaphore(engineDevice.device(), renderData.rdRenderSemaphore[i], nullptr);
            vkDestroySemaphore(engineDevice.device(), renderData.rdComputeSemaphore[i], nullptr);
            vkDestroySemaphore(engineDevice.device(), renderData.rdGraphicSemaphore[i], nullptr);
            vkDestroyFence(engineDevice.device(), renderData.rdRenderFence[i], nullptr);
            vkDestroyFence(engineDevice.device(), renderData.rdComputeFence[i], nullptr);
        }
    }
}