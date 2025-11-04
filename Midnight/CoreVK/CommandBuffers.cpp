#include "CommandBuffers.h"
#include <cstdio>
#include "CoreVK/VkRenderData.h"
#include "CoreVK/swapchain.h"

namespace aveng {
    bool CommandBuffer::init(EngineDevice& engineDevice, std::vector<VkCommandBuffer>& commandBuffers) {
        
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        // Command buffer memory is allocated from a command pool
        allocInfo.commandPool = engineDevice.commandPoolGraphics();
        allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

        if (vkAllocateCommandBuffers(engineDevice.device(), &allocInfo, commandBuffers.data()) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate command buffers.");
        }

    }

    bool CommandBuffer::reset(VkCommandBuffer& commandBuffer, VkCommandBufferResetFlags flags) {
        VkResult result = vkResetCommandBuffer(commandBuffer, flags);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not reset command buffer (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        return true;
    }

    bool CommandBuffer::begin(VkCommandBuffer& commandBuffer, VkCommandBufferBeginInfo& beginInfo) {
        VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not begin new command buffer\n", __FUNCTION__);
            return false;
        }
        return true;
    }

    bool CommandBuffer::beginSingleShot(VkCommandBuffer& commandBuffer) {
        VkCommandBufferBeginInfo cmdBeginInfo{};
        cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        VkResult result = vkBeginCommandBuffer(commandBuffer, &cmdBeginInfo);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not begin new command buffer (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        return true;
    }

    bool CommandBuffer::end(VkCommandBuffer& commandBuffer) {
        VkResult result = vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS) {
            std::printf("%s error: could not end render pass (error: %i)\n", __FUNCTION__, result);
            return false;
        }
        return true;
    }

    VkCommandBuffer CommandBuffer::createSingleShotBuffer(VkRenderData renderData, EngineDevice& engineDevice, VkCommandPool pool) {
        std::printf("%s: creating a single shot command buffer\n", __FUNCTION__);
        std::vector<VkCommandBuffer> commandBuffer(1);

        if (!init(engineDevice, commandBuffer)) {
            std::printf("%s error: could not create command buffer\n", __FUNCTION__);
            return VK_NULL_HANDLE;
        }

        VkResult result = vkResetCommandBuffer(commandBuffer[0], 0);
        if (result != VK_SUCCESS) {
            std::printf("%s error: failed to reset command buffer (error: %i)\n", __FUNCTION__, result);
            return VK_NULL_HANDLE;
        }

        VkCommandBufferBeginInfo cmdBeginInfo{};
        cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        result = vkBeginCommandBuffer(commandBuffer, &cmdBeginInfo);
        if (result != VK_SUCCESS) {
            std::printf("%s error: failed to begin command buffer (error: %i)\n", __FUNCTION__, result);
            return VK_NULL_HANDLE;
        }

        std::printf("%s: single shot command buffer successfully created\n", __FUNCTION__);
        return commandBuffer;
    }

    bool CommandBuffer::submitSingleShotBuffer(VkRenderData renderData, VkCommandPool pool,
        VkCommandBuffer commandBuffer, VkQueue queue) {
        std::printf("%s: submitting single shot command buffer\n", __FUNCTION__);

        VkResult result = vkEndCommandBuffer(commandBuffer);
        if (result != VK_SUCCESS) {
            std::printf("%s error: failed to end command buffer (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VkFence bufferFence;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        result = vkCreateFence(renderData.rdVkbDevice.device, &fenceInfo, nullptr, &bufferFence);
        if (result != VK_SUCCESS) {
            std::printf("%s error: failed to create buffer fence (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        result = vkResetFences(renderData.rdVkbDevice.device, 1, &bufferFence);
        if (result != VK_SUCCESS) {
            std::printf("%s error: buffer fence reset failed (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        result = vkQueueSubmit(queue, 1, &submitInfo, bufferFence);
        if (result != VK_SUCCESS) {
            std::printf("%s error: failed to submit buffer copy command buffer (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        result = vkWaitForFences(renderData.rdVkbDevice.device, 1, &bufferFence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) {
            std::printf("%s error: waiting for buffer fence failed (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        vkDestroyFence(renderData.rdVkbDevice.device, bufferFence, nullptr);
        cleanup(renderData, pool, commandBuffer);

        std::printf("%s: single shot command buffer successfully submitted\n", __FUNCTION__);
        return true;
    }

    void CommandBuffer::cleanup(VkRenderData renderData, VkCommandPool pool, VkCommandBuffer commandBuffer) {
        vkFreeCommandBuffers(renderData.rdVkbDevice.device, pool, 1, &commandBuffer);
    }

}