#pragma once

#include <vulkan/vulkan.h>
#include "AMD/vk_mem_alloc.h"
#include "CoreVK/VkRenderData.h"

// std lib headers
#include <memory>
#include <string>
#include <vector>

namespace aveng {

    class EngineDevice;

    class SwapChain {
    public:
        static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

        SwapChain(VkRenderData& renderData, EngineDevice& deviceRef, VkExtent2D windowExtent);
        SwapChain(VkRenderData& renderData, EngineDevice& deviceRef, VkExtent2D windowExtent, std::shared_ptr<SwapChain> previous);
        ~SwapChain();

        SwapChain(const SwapChain&) = delete;
        SwapChain& operator=(const SwapChain&) = delete;

        VkFormat        findDepthFormat();
        VkSwapchainKHR  getSwapchain() { return swapChain; }
        uint32_t        width() { return swapChainExtent.width; }
        uint32_t        height() { return swapChainExtent.height; }
        size_t          imageCount() { return swapChainImages.size(); }
        VkExtent2D      getSwapChainExtent() { return swapChainExtent; }
        VkFormat        getSwapChainImageFormat() { return swapChainImageFormat; }

        VkRenderPass    getRenderPass() { return mRenderPass; }
        // VkRenderPass getSecondaryRenderPass() { return mSecondaryRenderPass; } // This can be acquired from renderData

        // Framebuffers & Views
        std::vector<VkImageView>& getSwapChainImageViews() { return swapChainImageViews; }
        VkImageView     getImageView(int index) { return swapChainImageViews[index]; }
        VkFramebuffer   getFrameBuffer(int index) { return mSwapChainFramebuffers[index]; }
        VkFramebuffer   getSelectionFrameBuffer(int index) { return mSelectionFramebuffers[index]; }
        VkImageView     getSelectionImageView(int index) { return mSelectionImageViews[index]; }

        VkImageView     createImageView(VkImage image, VkFormat format);
        void            createSelectionImageView(size_t index);
        VkImage&        getImage(int index) { return swapChainImages[index]; }
        size_t          swapChainImagesSize() { return swapChainImages.size(); }

        /*
        * Read the value of a pixel which contains the selected instance ID
        */
        float getPixelValueFromPos(unsigned int xPos, unsigned int yPos, uint32_t frameIndex);

        //void createTextureImageViews();
        bool createSecondaryRenderpass(VkRenderPass& renderPass);
        bool createSelectionRenderpass(VkRenderPass& renderPass);

        float extentAspectRatio() {
            return static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
        }

        bool compareSwapFormats(const SwapChain& swapChain) const 
        {
            // If these 2 formats are the same throughout, the swapchain must be compatible with the renderpass
            return swapChain.swapChainDepthFormat == swapChainDepthFormat && 
                   swapChain.swapChainImageFormat == swapChainImageFormat;
        }

        bool createEditorSelectionFramebuffers();

    private:
        void init();
        void createSwapChain();
        void createImageViews();
        void createSelectionImageViews();
        void createDepthResources();
        void createRenderPass();
        void createFramebuffers();

        // Helper functions
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

        VkFormat swapChainImageFormat;
        VkFormat swapChainDepthFormat;
        VkExtent2D swapChainExtent;
        
        std::vector<VkFramebuffer> mSelectionFramebuffers;
        std::vector<VkFramebuffer> mSwapChainFramebuffers;
        VkRenderPass mRenderPass;
        //VkRenderPass mSecondaryRenderPass;            // Stored in renderData
        //VkRenderPass rdSelectionRenderpass;           // Stored in renderData

        std::vector<VmaAllocation> depthImageAllocations;
        std::vector<VkImage> depthImages;
        std::vector<VkImageView> depthImageViews;
        std::vector<VkImage> swapChainImages;
        std::vector<VkImageView> swapChainImageViews;
        std::vector<VkImageView> mSelectionImageViews;

        EngineDevice& device;
        VkExtent2D windowExtent;

        VkSwapchainKHR swapChain;
        std::shared_ptr<SwapChain> oldSwapChain;
        VkRenderData& renderData;

    };

} 