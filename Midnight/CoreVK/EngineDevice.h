#pragma once
#include "AMD/vk_mem_alloc.h"
#include <string>
#include <vector>

namespace aveng {
    class AvengWindow;

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    // Used in our search for queue families supported by our gfx device
    struct QueueFamilyIndices {
        uint32_t graphicsFamily;
        uint32_t presentFamily;
        uint32_t computeFamily; // This could cause issues for devices without support. We haven't fully implemented a fallback.
                                // One big TODO is to make sure we can ship games that don't use compute shaders at all (no animation support)
        bool graphicsFamilyHasValue = false;
        bool presentFamilyHasValue = false;
        bool computeFamilyHasValue = false;
        bool isComplete() { return graphicsFamilyHasValue && presentFamilyHasValue && computeFamilyHasValue; }
    };

    class EngineDevice {

        VkInstance _instance;

        // This tells Vulkan about the callback funtion for our validation layer debug
        VkDebugUtilsMessengerEXT debugMessenger;

        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        VkCommandPool   _commandPoolGraphics;
        VkCommandPool   _commandPoolCompute;
        VkCommandPool   _commandPoolRuntimeGraphics;
        VkCommandPool   _commandPoolRuntimeCompute;
        VkDevice        _device;
        VkSurfaceKHR    _surface;
        VkQueue         _graphicsQueue;
        VkQueue         _presentQueue;
        VkQueue         _computeQueue;
        VmaAllocator    _allocator;

        AvengWindow& aveng_window;

    public:

#ifdef _DEBUG
          const bool enableValidationLayers = true;
#else
          const bool enableValidationLayers = false;
#endif

        EngineDevice(AvengWindow& window);
        ~EngineDevice();

        // Not copyable or movable
        EngineDevice(const EngineDevice &) = delete;
        EngineDevice& operator=(const EngineDevice &) = delete;
        EngineDevice(EngineDevice &&) = delete;
        EngineDevice &operator=(EngineDevice &&) = delete;

        // Getters
        VkInstance instance()                   { return _instance; }
        VkPhysicalDevice physicalDevice()       { return _physicalDevice; }
        VkCommandPool commandPoolGraphics()     { return _commandPoolGraphics; }
        VkCommandPool commandPoolCompute()      { return _commandPoolCompute; }
        VkCommandPool commandPoolRuntimeGraphics()     { return _commandPoolRuntimeGraphics; }
        VkCommandPool commandPoolRuntimeCompute()      { return _commandPoolRuntimeCompute; }

        VkDevice device()                       { return _device; }
        VkSurfaceKHR surface()                  { return _surface; }
        VkQueue graphicsQueue()                 { return _graphicsQueue; }
        VkQueue computeQueue()                  { return _computeQueue; }
        VkQueue presentQueue()                  { return _presentQueue; }
        bool sameGraphicsComputeQueue() const   { return _graphicsQueue == _computeQueue; }
        VmaAllocator allocator()                { return _allocator; }
        void checkBufferCoherence(VmaAllocation& allocation);

        SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(_physicalDevice); }
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        QueueFamilyIndices findPhysicalQueueFamilies(){ return findQueueFamilies(_physicalDevice); };
        uint32_t getGraphicsQueueFamily() { return findPhysicalQueueFamilies().graphicsFamily; }
        VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

        // Buffer Helper Functions
        void createBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties,
            VkBuffer &buffer,
            VkDeviceMemory &bufferMemory
        );

        // VMA-based buffer creation
        void createBufferWithVMA(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VmaMemoryUsage memoryUsage,
            VkBuffer &buffer,
            VmaAllocation &allocation,
            VmaAllocationCreateFlags flags = 0
        );

        // V2
        VkCommandBuffer createSingleShotBuffer();  // allocates a new one-time buffer. Deprecate this, probably
        bool initCommandBuffers(std::vector<VkCommandBuffer>& commandBuffers, const char* type = "graphics");
        bool beginSingleShotCommand(VkCommandBuffer& commandBuffer); // Reuses a reset command buffer (faster)
        bool beginCommandBuffer(VkCommandBuffer& commandBuffer, VkCommandBufferBeginInfo& beginInfo);
        bool submitSingleShotBuffer(VkCommandBuffer commandBuffer); // Note: Uses the graphics queue
        bool submitRuntimeCmdBuffer(VkCommandBuffer commandBuffer);
        bool resetCommandBuffer(VkCommandBuffer& commandBuffer, VkCommandBufferResetFlags flags = 0);
        bool endCommandBuffer(VkCommandBuffer& commandBuffer);
        void cleanupCommandBuffer(VkCommandPool& pool, VkCommandBuffer& commandBuffer);

        // VMA-based image creation
        void createImageWithVMA(
            const VkImageCreateInfo &imageInfo,
            VmaMemoryUsage memoryUsage,
            VkImage &image,
            VmaAllocation &allocation
        );

        // VMA Memory Budget Monitoring
        void checkMemoryBudget();
        void printMemoryStats();
        bool isMemoryPressureHigh(); // Returns true if >80% memory used

        VkPhysicalDeviceProperties properties;

    private:
        void createInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createCommandPools();

        // helper functions
        bool isDiscreteDeviceSuitable(VkPhysicalDevice device);
        bool isAnyDeviceSuitable(VkPhysicalDevice device);
        std::vector<const char *> getRequiredExtensions();
        bool checkValidationLayerSupport();
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo);
        void hasGflwRequiredInstanceExtensions();
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

        /*
            Extensions
        */
        // Validation layer to be enabled
        const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};
        // Extensions to be enabled
        const std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    };

}