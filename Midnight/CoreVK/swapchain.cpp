#include "swapchain.h"
#include "CoreVK/EngineDevice.h"
#include "Utils/Logger.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <algorithm>

namespace aveng {

    SwapChain::SwapChain(VkRenderData& renderData, EngineDevice& deviceRef, VkExtent2D extent)
        : device{ deviceRef }, windowExtent{ extent }, renderData{ renderData } {
        init();
    }

    SwapChain::SwapChain(VkRenderData& renderData, EngineDevice& deviceRef, VkExtent2D extent, std::shared_ptr<SwapChain> previous)
        : device{ deviceRef }, windowExtent{ extent }, oldSwapChain{previous}, renderData{ renderData } {
        init();

        // clean up old swap chain
        oldSwapChain = nullptr;
    }

    void SwapChain::init()
    {
        /**
        * Note: The editor is responsible for ensuring the order of swapchain resource creation.
        * Selection image views are the only current exception. The renderer might utilize them too.
        */
        createSwapChain();
        createImageViews();
        createSelectionImageViews();
        createRenderPass();
        createDepthResources();
        createFramebuffers();
        // createEditorSelectionFramebuffers();
    }

    SwapChain::~SwapChain() {

        std::cout << "DESTROYING SWAPCHAIN!!" << std::endl;

        for (auto imageView : swapChainImageViews) {
            vkDestroyImageView(device.device(), imageView, nullptr);
            
        }

        // Cleanup selection images from renderData
        for (size_t i = 0; i < swapChainImages.size(); i++) {
            if (renderData.rdSelectionImageViews[i] != VK_NULL_HANDLE) {
                vkDestroyImageView(device.device(), renderData.rdSelectionImageViews[i], nullptr);
            }
            if (renderData.rdSelectionImages[i] != VK_NULL_HANDLE) {
                vmaDestroyImage(device.allocator(), renderData.rdSelectionImages[i], 
                                renderData.rdSelectionImageAllocs[i]);
            }
        }

        swapChainImageViews.clear();
        // mSelectionImageViews.clear();
        renderData.rdSelectionImages.clear();
        renderData.rdSelectionImageViews.clear();
        renderData.rdSelectionImageAllocs.clear();

        if (swapChain != nullptr) {
            vkDestroySwapchainKHR(device.device(), swapChain, nullptr);
            swapChain = nullptr;
        }

        std::cout << "Destroying Depth Images" << std::endl;
        for (int i = 0; i < depthImages.size(); i++) {
            vkDestroyImageView(device.device(), depthImageViews[i], nullptr);
            vmaDestroyImage(device.allocator(), depthImages[i], depthImageAllocations[i]);
        }

        // Primary Framebuffers
        for (auto framebuffer : mSwapChainFramebuffers) {
            vkDestroyFramebuffer(device.device(), framebuffer, nullptr);
        }
        mSwapChainFramebuffers.clear();
        // Selection Framebuffers
        for (auto& fb : mSelectionFramebuffers) {
            vkDestroyFramebuffer(device.device(), fb, nullptr);
        }
        mSelectionFramebuffers.clear();

        // Destroy the primary renderpass
        vkDestroyRenderPass(device.device(), mRenderPass, nullptr);

    }

    void SwapChain::createSwapChain() 
    {
        SwapChainSupportDetails swapChainSupport = device.getSwapChainSupport();
        VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
        VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
        VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

        uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
        if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            imageCount = std::max(uint32_t{ 3 }, swapChainSupport.capabilities.minImageCount + 1);
        }
        
        if (swapChainSupport.capabilities.maxImageCount > 0 && 
            imageCount > swapChainSupport.capabilities.maxImageCount) 
        {
            imageCount = swapChainSupport.capabilities.maxImageCount;
        }

        std::cout << "Swapchain Image count: " << imageCount << std::endl;

        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = device.surface();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = device.findPhysicalQueueFamilies();
        uint32_t queueFamilyIndices[] = { indices.graphicsFamily, indices.presentFamily };

        if (indices.graphicsFamily != indices.presentFamily) {
            createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            createInfo.queueFamilyIndexCount = 2;
            createInfo.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.queueFamilyIndexCount = 0;      // Optional
            createInfo.pQueueFamilyIndices = nullptr;  // Optional
        }

        createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
        createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        createInfo.presentMode = presentMode;
        createInfo.clipped = VK_TRUE;

        createInfo.oldSwapchain == nullptr ? VK_NULL_HANDLE : oldSwapChain->swapChain;

        if (vkCreateSwapchainKHR(device.device(), &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
            throw std::runtime_error("failed to create swap chain!");
        }

        // we only specified a minimum number of images in the swap chain, so the implementation is
        // allowed to create a swap chain with more. That's why we'll first query the final number of
        // images with vkGetSwapchainImagesKHR, then resize the container and finally call it again to
        // retrieve the handles.
        vkGetSwapchainImagesKHR(device.device(), swapChain, &imageCount, nullptr);
        swapChainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(device.device(), swapChain, &imageCount, swapChainImages.data());

        swapChainImageFormat = surfaceFormat.format;
        swapChainExtent = extent;
    }

    VkImageView SwapChain::createImageView(VkImage image, VkFormat format)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkImageView imageView;
        if (vkCreateImageView(device.device(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
            throw std::runtime_error("failed to create texture image view!");
        }

        return imageView;
    }

    void SwapChain::createImageViews() {
        swapChainImageViews.resize(swapChainImages.size());
        for (size_t i = 0; i < swapChainImages.size(); i++)
        {
            std::cout << "Creating SwapChain Img View " << i << std::endl;
            swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat);
        }
    }

    /**
    * Note that we're using swapChainImages.size() because this simply alludes to the number of
    * images that the swapchain uses for rendering.
    * Creates one selection image per swapchain frame for proper synchronization.
    */
    void SwapChain::createSelectionImageViews() {

        size_t imageCount = swapChainImages.size();
        renderData.rdSelectionImages.resize(imageCount);        // TODO - make all 3 of these local to SwapChain to trim down VkRenderData
        renderData.rdSelectionImageViews.resize(imageCount);
        renderData.rdSelectionImageAllocs.resize(imageCount);
        // mSelectionImageViews.resize(imageCount);
        
        for (size_t i = 0; i < imageCount; i++)
        {
            std::cout << "Creating SwapChain Selection Img View " << i << std::endl;
            createSelectionImageView(i);
            // mSelectionImageViews[i] = renderData.rdSelectionImageViews[i];
        }
    }

    void SwapChain::createSelectionImageView(size_t index)
    {
        VkExtent3D selectionImageExtent = {
            width(),
            height(),
            1
        };

        renderData.rdSelectionFormat = VK_FORMAT_R32_UINT; // Update this to VK_FORMAT_R32_UINT for the new selection design 
                                                                // - Fragment shader output must be uint 
                                                                // - Attachment description must not have blending enabled 
                                                                // - Your subpass attachment reference must match integer format rules
                                                                // - Layout transitions still use COLOR_ATTACHMENT_OPTIMAL

        VkImageCreateInfo selecImageInfo{};
        selecImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        selecImageInfo.imageType = VK_IMAGE_TYPE_2D;
        selecImageInfo.format = renderData.rdSelectionFormat;
        selecImageInfo.extent = selectionImageExtent;
        selecImageInfo.mipLevels = 1;
        selecImageInfo.arrayLayers = 1;
        selecImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        selecImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        selecImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo selectionAllocInfo{};
        selectionAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        selectionAllocInfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkResult result = vmaCreateImage(device.allocator(), &selecImageInfo, &selectionAllocInfo,
            &renderData.rdSelectionImages[index], &renderData.rdSelectionImageAllocs[index], nullptr);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not allocate selection buffer memory for index %zu (error: %i)\n", __FUNCTION__, index, result);
            throw std::runtime_error("Fail");
        }

        VkImageViewCreateInfo selectionImageViewinfo{};
        selectionImageViewinfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        selectionImageViewinfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        selectionImageViewinfo.image = renderData.rdSelectionImages[index];
        selectionImageViewinfo.format = renderData.rdSelectionFormat;
        selectionImageViewinfo.subresourceRange.baseMipLevel = 0;
        selectionImageViewinfo.subresourceRange.levelCount = 1;
        selectionImageViewinfo.subresourceRange.baseArrayLayer = 0;
        selectionImageViewinfo.subresourceRange.layerCount = 1;
        selectionImageViewinfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

        result = vkCreateImageView(device.device(), &selectionImageViewinfo,
            nullptr, &renderData.rdSelectionImageViews[index]);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error: could not create selection buffer image view for index %zu (error: %i)\n", __FUNCTION__, index, result);
            throw std::runtime_error("Fail");
        }

        // /* Transition newly created image from UNDEFINED to COLOR_ATTACHMENT_OPTIMAL */
         VkCommandBuffer transitionCmd = device.createSingleShotBuffer();

         VkImageSubresourceRange range{};
         range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
         range.baseMipLevel = 0;
         range.levelCount = 1;
         range.baseArrayLayer = 0;
         range.layerCount = 1;

         VkImageMemoryBarrier barrier{};
         barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
         barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
         barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
         barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
         barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
         barrier.image = renderData.rdSelectionImages[index];
         barrier.subresourceRange = range;
         barrier.srcAccessMask = 0;
         barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

         vkCmdPipelineBarrier(
             transitionCmd,
             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
             0,
             0, nullptr,
             0, nullptr,
             1, &barrier
         );

         if (!device.submitSingleShotBuffer(transitionCmd)) {
             Logger::log(1, "%s error: could not transition selection image %zu to COLOR_ATTACHMENT_OPTIMAL\n", __FUNCTION__, index);
             throw std::runtime_error("Failed to transition selection image layout");
         }
    }

    /**
     * Primary Renderpass
     */
    void SwapChain::createRenderPass() {

        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = getSwapChainImageFormat();
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;          // Required to display to the screen

        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;   // very optimal

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = findDepthFormat();
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // Was previously LOAD_OP_DONT_CARE
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef{};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDesc = {};
        subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc.colorAttachmentCount = 1;
        subpassDesc.pColorAttachments = &colorAttachmentRef;
        subpassDesc.pDepthStencilAttachment = &depthAttachmentRef;

        // Dependencies are like pipeline barriers within a renderpass to protect the swapchain image
        VkSubpassDependency subpassDep{};
        subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep.dstSubpass = 0;
        subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.srcAccessMask = 0;
        subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkSubpassDependency depthDep{};
        depthDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        depthDep.dstSubpass = 0;
        depthDep.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depthDep.srcAccessMask = 0;
        depthDep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depthDep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::vector<VkSubpassDependency> dependencies = { subpassDep, depthDep };
        std::vector<VkAttachmentDescription> attachments = { colorAttachment, depthAttachment };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDesc;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        if (vkCreateRenderPass(device.device(), &renderPassInfo, nullptr, &mRenderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render pass!");
        }
    }

    /**
    * Secondary renderpasses are assumed to have data ready for Vulkan to continue rendering to.
    * Vulkan assumes the attachments are already valid when the renderpass begins. (LOAD_OP_LOAD)
    * 
    * The data in the attachment is presumably useful and will be kept for later use (STORE_OP_STORE)
    */
    bool SwapChain::createSecondaryRenderpass(VkRenderPass& renderPass)
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format = getSwapChainImageFormat(); // mSwapChainImageFormat
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        /* load previous image */
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        /* must be previous image format */
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttRef{};
        colorAttRef.attachment = 0;
        colorAttRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAtt{};
        depthAtt.flags = 0;
        depthAtt.format = findDepthFormat();
        depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttRef{};
        depthAttRef.attachment = 1;
        depthAttRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDesc{};
        subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc.colorAttachmentCount = 1;
        subpassDesc.pColorAttachments = &colorAttRef;
        subpassDesc.pDepthStencilAttachment = &depthAttRef;

        VkSubpassDependency subpassDep{};
        subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep.dstSubpass = 0;
        subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.srcAccessMask = 0;
        subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkSubpassDependency depthDep{};
        depthDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        depthDep.dstSubpass = 0;
        depthDep.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depthDep.srcAccessMask = 0;
        depthDep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depthDep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::vector<VkSubpassDependency> dependencies = { subpassDep, depthDep };
        std::vector<VkAttachmentDescription> attachments = { colorAtt, depthAtt };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDesc;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        VkResult result = vkCreateRenderPass(device.device(), &renderPassInfo, nullptr, &renderPass);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error; could not create renderpass (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        return true;

    }

    bool SwapChain::createSelectionRenderpass(VkRenderPass& renderPass)
    {

        std::cout << "Creating Selection Renderpass with format: " << renderData.rdSelectionFormat << std::endl;
    
        VkAttachmentDescription colorAtt{};
        colorAtt.format = getSwapChainImageFormat();
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;      // Note to self: LOAD_OP_CLEAR is a great indicator that this renderpass will occur before any other renderpasses
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttRef{};
        colorAttRef.attachment = 0;
        colorAttRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        /* separate selection buffer */
        VkAttachmentDescription selectionColorAtt{};
        selectionColorAtt.format = renderData.rdSelectionFormat;
        selectionColorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        selectionColorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        selectionColorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        selectionColorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        selectionColorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        selectionColorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        selectionColorAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference selectionColorAttRef{};
        selectionColorAttRef.attachment = 1;
        selectionColorAttRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAtt{};
        depthAtt.flags = 0;
        depthAtt.format = swapChainDepthFormat; // renderData.rdDepthFormat;
        depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttRef{};
        depthAttRef.attachment = 2;
        depthAttRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        std::vector<VkAttachmentReference> attachmentRefs = { colorAttRef, selectionColorAttRef };

        VkSubpassDescription subpassDesc{};
        subpassDesc.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDesc.colorAttachmentCount = static_cast<uint32_t>(attachmentRefs.size());
        subpassDesc.pColorAttachments = attachmentRefs.data();
        subpassDesc.pDepthStencilAttachment = &depthAttRef;

        VkSubpassDependency subpassDep{};
        subpassDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        subpassDep.dstSubpass = 0;
        subpassDep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.srcAccessMask = 0;
        subpassDep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        subpassDep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkSubpassDependency depthDep{};
        depthDep.srcSubpass = VK_SUBPASS_EXTERNAL;
        depthDep.dstSubpass = 0;
        depthDep.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depthDep.srcAccessMask = 0;
        depthDep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        depthDep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::vector<VkSubpassDependency> dependencies = { subpassDep, depthDep };
        std::vector<VkAttachmentDescription> attachments = { colorAtt, selectionColorAtt, depthAtt };

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDesc;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        VkResult result = vkCreateRenderPass(device.device(), &renderPassInfo, nullptr, &renderPass);
        if (result != VK_SUCCESS) {
            Logger::log(1, "%s error; could not create selection renderpass (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        return true;
    
    }

    void SwapChain::createFramebuffers() 
    {
        mSwapChainFramebuffers.resize(imageCount());
        for (size_t i = 0; i < imageCount(); i++) {
            std::array<VkImageView, 2> attachments = { swapChainImageViews[i], depthImageViews[i] };

            VkExtent2D swapChainExtent = getSwapChainExtent();
            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = mRenderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1;
            std::cout << "Creating mSwapChainFramebuffers [" << i << "]" << std::endl;
            if (vkCreateFramebuffer(device.device(), &framebufferInfo, nullptr, &mSwapChainFramebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }

    bool SwapChain::createEditorSelectionFramebuffers()
    {
        mSelectionFramebuffers.resize(swapChainImageViews.size());

        for (unsigned int i = 0; i < swapChainImageViews.size(); ++i) {
            std::vector<VkImageView> attachments = { swapChainImageViews[i],
              renderData.rdSelectionImageViews[i], depthImageViews[i]};

            VkFramebufferCreateInfo FboInfo{};
            FboInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            FboInfo.renderPass = renderData.rdSelectionRenderpass;
            FboInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            FboInfo.pAttachments = attachments.data();
            FboInfo.width = width();
            FboInfo.height = height();
            FboInfo.layers = 1;
            std::cout << "Creating mSelectionFramebuffers [" << i << "]" << std::endl;
            VkResult result = vkCreateFramebuffer(device.device(), &FboInfo, nullptr, &mSelectionFramebuffers[i]);
            if (result != VK_SUCCESS) {
                Logger::log(1, "%s error: failed to create selection framebuffer %i (error: %i)\n", __FUNCTION__, i, result);
                return false;
            }
        }
        return true;
    
    }

    int SwapChain::getPixelValueFromPos(unsigned int xPos, unsigned int yPos, uint32_t imageIndex) {
        /* random default value to detect errors */
        int pixelColor = -444;

        // This would be true while the swapchain is being recreated.
        if (renderData.rdSelectionImages.empty() ||
            imageIndex >= renderData.rdSelectionImages.size()) {
            Logger::log(1, "%s error: selection images unavailable or invalid index\n", __FUNCTION__);
            return pixelColor;
        }

        /* Bounds check coordinates */
        if (xPos >= width() || yPos >= height()) {
            Logger::log(1, "%s error: coordinates out of bounds (%u, %u), max is (%u, %u)\n",
                __FUNCTION__, xPos, yPos, width() - 1, height() - 1);
            return pixelColor;
        }

        /* Bounds check frame index */
        if (imageIndex >= renderData.rdSelectionImages.size()) {
            Logger::log(1, "%s error: frame index %u out of bounds, max is %zu\n",
                __FUNCTION__, imageIndex, renderData.rdSelectionImages.size() - 1);
            return pixelColor;
        }

        std::cout << "Reading pixel from: \t(" << xPos << ", " << yPos << ")" << std::endl;

        VkImage readbackImage;
        VmaAllocation readbackImageAlloc;

        /* create local image */
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width();
        imageInfo.extent.height = height();
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R32_UINT;
        imageInfo.tiling = VK_IMAGE_TILING_LINEAR;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo imageAllocInfo{};
        imageAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        imageAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

        VkResult result = vmaCreateImage(device.allocator(), &imageInfo, &imageAllocInfo, &readbackImage, &readbackImageAlloc, nullptr);
        if (result != VK_SUCCESS) {
            vmaDestroyImage(device.allocator(), readbackImage, readbackImageAlloc);
            Logger::log(1, "%s error: could not allocate read back image image via VMA (error: %i)\n", __FUNCTION__, result);
            return pixelColor;
        }

        VkCommandBuffer readbackCommandBuffer = device.createSingleShotBuffer();

        VkImageSubresourceRange layoutTransferRange{};
        layoutTransferRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        layoutTransferRange.baseMipLevel = 0;
        layoutTransferRange.levelCount = 1;
        layoutTransferRange.baseArrayLayer = 0;
        layoutTransferRange.layerCount = 1;

        /* transition destination (local) image to transfer destination layout */
        VkImageMemoryBarrier layoutTransferBarrier{};
        layoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        layoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        layoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        layoutTransferBarrier.image = readbackImage;
        layoutTransferBarrier.subresourceRange = layoutTransferRange;
        layoutTransferBarrier.srcAccessMask = 0;
        layoutTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        /* transition source (selection) image to transfer source optimal layout */
        VkImageMemoryBarrier srcLayoutTransferBarrier{};
        srcLayoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        srcLayoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        srcLayoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcLayoutTransferBarrier.image = renderData.rdSelectionImages.at(imageIndex);      // The image resource we're getting our data from (frame-specific)
        srcLayoutTransferBarrier.subresourceRange = layoutTransferRange;
        srcLayoutTransferBarrier.srcAccessMask = 0;
        srcLayoutTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        /* copy selection image to local image */
        VkImageCopy imageCopyRegion{};
        imageCopyRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.srcSubresource.layerCount = 1;
        imageCopyRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageCopyRegion.dstSubresource.layerCount = 1;
        imageCopyRegion.extent.width = width();
        imageCopyRegion.extent.height = height();
        imageCopyRegion.extent.depth = 1;   // Full on

        /* transition destination (local) image to general layout to allow mapping */
        VkImageMemoryBarrier destLayoutTransferBarrier{};
        destLayoutTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        destLayoutTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        destLayoutTransferBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        destLayoutTransferBarrier.image = readbackImage;
        destLayoutTransferBarrier.subresourceRange = layoutTransferRange;
        destLayoutTransferBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        destLayoutTransferBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;

        vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &layoutTransferBarrier);
        vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &srcLayoutTransferBarrier);
        vkCmdCopyImage(readbackCommandBuffer, renderData.rdSelectionImages.at(imageIndex), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            readbackImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion);
        vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 0, nullptr, 1, &destLayoutTransferBarrier);

        /* transition selection image back to COLOR_ATTACHMENT_OPTIMAL for next frame */
        // This is more robust in our architecture because we use a double buffering approach
        VkImageMemoryBarrier restoreSelectionLayoutBarrier{};
        restoreSelectionLayoutBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        restoreSelectionLayoutBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        restoreSelectionLayoutBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        restoreSelectionLayoutBarrier.image = renderData.rdSelectionImages.at(imageIndex);
        restoreSelectionLayoutBarrier.subresourceRange = layoutTransferRange;
        restoreSelectionLayoutBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        restoreSelectionLayoutBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(readbackCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0, 0, nullptr, 0, nullptr, 1, &restoreSelectionLayoutBarrier);

        bool commandResult = device.submitSingleShotBuffer(readbackCommandBuffer); // Note: Uses the graphics queue
  
        if (!commandResult) {
            vmaDestroyImage(device.allocator(), readbackImage, readbackImageAlloc);
            Logger::log(1, "%s error: could not submit readback transfer commands\n", __FUNCTION__);
            return pixelColor;
        }

        /* get image layout */
        VkImageSubresource subResource{};
        subResource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        VkSubresourceLayout subResourceLayout{};

        vkGetImageSubresourceLayout(device.device(), readbackImage, &subResource, &subResourceLayout);

        /* map and read data */
        const unsigned int* data;
        result = vmaMapMemory(device.allocator(), readbackImageAlloc, (void**)&data);
        if (result != VK_SUCCESS) {
            vmaDestroyImage(device.allocator(), readbackImage, readbackImageAlloc);
            Logger::log(1, "%s error: could not map readback image memory (error: %i)\n", __FUNCTION__, result);
            return pixelColor;
        }

        data += yPos * subResourceLayout.rowPitch / sizeof(unsigned int) + xPos;
        pixelColor = *data;

        vmaUnmapMemory(device.allocator(), readbackImageAlloc);

        /* destroy local image, no longer needed */
        vmaDestroyImage(device.allocator(), readbackImage, readbackImageAlloc);
        std::cout << "Got pixelColor: " << pixelColor << std::endl;
        return pixelColor;
    }

    void SwapChain::createDepthResources() 
    {
        VkFormat depthFormat = findDepthFormat();
        swapChainDepthFormat = depthFormat;
        VkExtent2D swapChainExtent = getSwapChainExtent();

        depthImages.resize(imageCount());
        depthImageAllocations.resize(imageCount());
        depthImageViews.resize(imageCount());

        for (int i = 0; i < depthImages.size(); i++) {
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = swapChainExtent.width;
            imageInfo.extent.height = swapChainExtent.height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = depthFormat;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.flags = 0;

            device.createImageWithVMA(
                imageInfo,
                VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, // GPU-only memory for optimal depth buffer performance
                depthImages[i],
                depthImageAllocations[i]);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = depthImages[i];
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = depthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device.device(), &viewInfo, nullptr, &depthImageViews[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create texture image view!");
            }
        }
    }

    VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) 
    {
        for (const auto& availableFormat : availableFormats) 
        {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB 
                && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) 
            {
                return availableFormat;
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR SwapChain::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) 
    {
        // Pro-Tip: VK_PRESENT_MODE_MAILBOX_KHR is probably the most efficient, but more energy intensive so it might not be well suited for mobile applications.
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                std::cout << "Present mode: Mailbox" << std::endl;
                return availablePresentMode;
            }
        }

         // Pro-Tip: Immediate mode will not work on most mobile devices
         //for (const auto &availablePresentMode : availablePresentModes) {
         //  if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
         //    std::cout << "Present mode: Immediate" << std::endl;
         //    return availablePresentMode;
         //  }
         //}

        std::cout << "Present mode: V-Sync" << std::endl;
        return VK_PRESENT_MODE_FIFO_KHR;
        //return VK_PRESENT_MODE_IMMEDIATE_KHR;

    }

    VkExtent2D SwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) 
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) 
        {
            return capabilities.currentExtent;
        }
        else {
            VkExtent2D actualExtent = windowExtent;
            actualExtent.width = std::max(
                capabilities.minImageExtent.width,
                std::min(capabilities.maxImageExtent.width, actualExtent.width)
            );
            actualExtent.height = std::max(
                capabilities.minImageExtent.height,
                std::min(capabilities.maxImageExtent.height, actualExtent.height)
            );

            return actualExtent;
        }
    }

    VkFormat SwapChain::findDepthFormat() 
    {
        // TODO : Cool things to learn about
        return device.findSupportedFormat(
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }

}  // namespace aveng