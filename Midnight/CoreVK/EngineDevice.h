#pragma once

#include "../Core/aveng_window.h"
#include <string>
#include <vector>

namespace aveng {

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    // Used in our search for queue families supported by our gfx device
    struct QueueFamilyIndices {
        uint32_t graphicsFamily;
        uint32_t presentFamily;
        bool graphicsFamilyHasValue = false;
        bool presentFamilyHasValue = false;
        bool isComplete() { return graphicsFamilyHasValue && presentFamilyHasValue; }
    };

    class EngineDevice {

        VkInstance _instance;

        // This tells Vulkan about the callback funtion for our validation layer debug
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
        AvengWindow&    window;
        VkCommandPool   _commandPool;
        VkDevice        _device;
        VkSurfaceKHR    _surface;
        VkQueue         _graphicsQueue;
        VkQueue         _presentQueue;

    public:

//#ifdef NDEBUG
          const bool enableValidationLayers = true;
//#else
//          const bool enableValidationLayers = true;
//#endif

        EngineDevice(AvengWindow &window);
        ~EngineDevice();

        // Not copyable or movable
        EngineDevice(const EngineDevice &) = delete;
        EngineDevice& operator=(const EngineDevice &) = delete;
        EngineDevice(EngineDevice &&) = delete;
        EngineDevice &operator=(EngineDevice &&) = delete;

        // Getters
        VkInstance instance()                   { return _instance; }
        VkPhysicalDevice physicalDevice()       { return _physicalDevice; }
        VkCommandPool commandPool()             { return _commandPool; }
        VkDevice device()                       { return _device; }
        VkSurfaceKHR surface()                  { return _surface; }
        VkQueue graphicsQueue()                 { return _graphicsQueue; }
        VkQueue presentQueue()                  { return _presentQueue; }


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

        VkCommandBuffer beginSingleTimeCommands();

        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void copyBufferToImage(
            VkBuffer buffer, 
            VkImage image, 
            uint32_t width, 
            uint32_t height, 
            uint32_t layerCount
        );

        void createImageWithInfo(
            const VkImageCreateInfo &imageInfo,
            VkMemoryPropertyFlags properties,
            VkImage &image,
            VkDeviceMemory &imageMemory
        );

        VkPhysicalDeviceProperties properties;

    private:
        void createInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createCommandPool();

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