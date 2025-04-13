#pragma once
#pragma once

#include "EngineDevice.h"

// vulkan headers
#include <vulkan/vulkan.h>

// std lib headers
#include <memory>
#include <string>
#include <vector>

namespace aveng {

    class SwapChain {
    public:
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

        SwapChain(EngineDevice& deviceRef, VkExtent2D windowExtent);
        SwapChain(EngineDevice& deviceRef, VkExtent2D windowExtent, std::shared_ptr<SwapChain> previous);
        ~SwapChain();

        SwapChain(const SwapChain&) = delete;
        SwapChain& operator=(const SwapChain&) = delete;

        uint32_t            width() { return swapChainExtent.width; }
        uint32_t            height() { return swapChainExtent.height; }
        size_t              imageCount() { return swapChainImages.size(); }
        VkRenderPass        getRenderPass() { return renderPass; }
        VkExtent2D          getSwapChainExtent() { return swapChainExtent; }
        VkFormat            getSwapChainImageFormat() { return swapChainImageFormat; }
        VkFramebuffer       getFrameBuffer(int index) { return swapChainFramebuffers[index]; }

        VkImageView          createImageView(VkImage image, VkFormat format);
        VkImageView          getImageView(int index) { return swapChainImageViews[index]; }
        VkImage&             getImage(int index) { return swapChainImages[index]; }
        size_t               swapChainImagesSize() { return swapChainImages.size(); }
        std::vector<VkImageView>& getSwapChainImageViews() { return swapChainImageViews; }

        VkResult            submitCommandBuffers(const VkCommandBuffer* buffers, uint32_t* imageIndex);
        VkResult            acquireNextImage(uint32_t* imageIndex);
        VkFormat            findDepthFormat();

        void createTextureImageViews();

        float extentAspectRatio() {
            return static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
        }

        bool compareSwapFormats(const SwapChain& swapChain) const 
        {
            // If these 2 formats are the same throughout, the swapchain must be compatible with the renderpass
            return swapChain.swapChainDepthFormat == swapChainDepthFormat && 
                   swapChain.swapChainImageFormat == swapChainImageFormat;
        }

    private:
        void init();
        void createSwapChain();
        void createImageViews();
        void createDepthResources();
        void createRenderPass();
        void createFramebuffers();
        void createSyncObjects();

        // Helper functions
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(
            const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(
            const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

        VkFormat swapChainImageFormat;
        VkFormat swapChainDepthFormat;
        VkExtent2D swapChainExtent;

        std::vector<VkFramebuffer> swapChainFramebuffers;
        VkRenderPass renderPass;

        std::vector<VkImage> depthImages;
        std::vector<VkDeviceMemory> depthImageMemorys;
        std::vector<VkImageView> depthImageViews;
        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;

        EngineDevice& device;
        VkExtent2D windowExtent;

        VkSwapchainKHR swapChain;
        std::shared_ptr<SwapChain> oldSwapChain;

        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkFence> inFlightFences;
        std::vector<VkFence> imagesInFlight;
        size_t currentFrame = 0;

        VkImageView textureImageView;

    };

}  // namespace lve