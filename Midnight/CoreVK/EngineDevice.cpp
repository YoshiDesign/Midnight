#include "EngineDevice.h"

#include <cstring>
#include <iostream>
#include <set>
#include <unordered_set>

namespace aveng {

    // Local callback functions for validation layer
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,     // There are 4 levels of severity, you can compare them
        VkDebugUtilsMessageTypeFlagsEXT messageType,                // Another enum of types to help narrow down the incident
        const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,  // Refers to a struct full of useful debug information
        void *pUserData                                             // Contains a pointer, that was specified during setup of the callback. Allows us to pass data to it
    ) {
      std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

      // Always return false unless you're trying to test the validation layer itself.
      return VK_FALSE;
    }

    VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
        const VkAllocationCallbacks *pAllocator,
        VkDebugUtilsMessengerEXT *pDebugMessenger
    ) {

      auto func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr (
          instance,
          "vkCreateDebugUtilsMessengerEXT"
      );

        if (func != nullptr) 
        {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } 
        else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

    }

    /*
     *  Free our Debug utility
     */
    void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT debugMessenger,
        const VkAllocationCallbacks *pAllocator
    ) {
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr (
            instance,
            "vkDestroyDebugUtilsMessengerEXT"
        );

        if (func != nullptr) 
        {
            func(instance, debugMessenger, pAllocator);
        }
    }

    /**
     * Class Functions
     */
    // Default Constructor - Initialize Vulkan
    EngineDevice::EngineDevice(AvengWindow &window) : window{window} 
    {

        // Create the Vulkan instance
        createInstance();

        // Enable Debug validation layer -- Disable for Release Build
        setupDebugMessenger();

        // The surface is Vulkan's connection to our Window from GLFW.
        // This calls createWindowSurface from our _window class which is why
        // EnginDevice is constructed with a AvengWindow reference.
        createSurface();

        // Choose your weapon (GPU), or multiple of them (super advanced)
        pickPhysicalDevice();

        // Determine thefeatures of our GPU we will be utilizing
        createLogicalDevice();

        // For command buffer allocation
        createCommandPool();
    }

    // Destructor
    EngineDevice::~EngineDevice() 
    {
        vkDestroyCommandPool(_device, _commandPool, nullptr);
        vkDestroyDevice(_device, nullptr);

        if (enableValidationLayers) 
        {
            DestroyDebugUtilsMessengerEXT(_instance, debugMessenger, nullptr);
        }

          vkDestroySurfaceKHR(_instance, _surface, nullptr);
          vkDestroyInstance(_instance, nullptr);
    }

    void EngineDevice::createInstance() 
    {

        // It's on the tin
        if (enableValidationLayers && !checkValidationLayerSupport()) 
        {
            throw std::runtime_error("validation layers requested, but not available!");
        }

        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Avenge - Zero";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Avenge";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;

        auto extensions = getRequiredExtensions();
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        if (enableValidationLayers) {
            // [Validators] Create this instance's Debug Validation Layer
            VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;

            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();

            populateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;

        } 
        else {
            createInfo.enabledLayerCount = 0;
            createInfo.pNext = nullptr;
        }

        if (vkCreateInstance(&createInfo, nullptr, &_instance) != VK_SUCCESS) 
        {
            throw std::runtime_error("failed to create instance!");
        }

        hasGflwRequiredInstanceExtensions();

    }

    /**
     * Pick a GFX card in a series of steps
     * - Enumeration
     * - Evaluation - Make sure the device is suitable for Vulkan in its current context
     * Allows for ranking of most capable - See https://vulkan-tutorial.com/en/Drawing_a_triangle/Setup/Physical_devices_and_queue_families - Base Device Suitability Checks
     */
    void EngineDevice::pickPhysicalDevice() 
    {

        // Enumerate your gfx devices
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);
        if (deviceCount == 0) 
        {
            throw std::runtime_error("failed to find GPUs with Vulkan support!");
        }

        std::cout << "Device count: " << deviceCount << std::endl;

        // Handle to our physical devices to be evaluated
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());

        // Prefer a discreet GPU
        for (const auto &device : devices) 
        {
            if (isDiscreteDeviceSuitable(device)) 
            {
                _physicalDevice = device;
                break;
            }
        }

        // If no discreet GPU is present, use integrated graphics.
        // Very quick implementation. (See: https://docs.vulkan.org/tutorial/latest/03_Drawing_a_triangle/00_Setup/03_Physical_devices_and_queue_families.html for a better one)
        if (_physicalDevice == VK_NULL_HANDLE) 
        {
            for (const auto& device : devices)
            {
                if (isAnyDeviceSuitable(device))
                {
                    _physicalDevice = device;
                    break;
                }
            }
            if (_physicalDevice == VK_NULL_HANDLE)
            {
                throw std::runtime_error("failed to find a suitable GPU!");
            }
        }

        vkGetPhysicalDeviceProperties(_physicalDevice, &properties);
        std::cout << "physical device: " << properties.deviceName << std::endl;
    }

    /*
     * Creation of a logical device.
     * This is the Vulkan instance's representation of our physical device.
     */
    void EngineDevice::createLogicalDevice() {
        /*
        * Get an enumeration of our required queue families. 
        * Note that these were already queried while selecting a suitable device.
        */
        QueueFamilyIndices indices = findQueueFamilies(_physicalDevice);

        // For config
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily, indices.presentFamily};

        // Configure the CreateInfo for each Queue, adding each to our vector
        float queuePriority = 1.0f;
        for (uint32_t queueFamily : uniqueQueueFamilies) 
        {
            // Create a new config
            VkDeviceQueueCreateInfo queueCreateInfo = {};
            queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueCreateInfo.queueFamilyIndex = queueFamily;
            queueCreateInfo.queueCount = 1;
            queueCreateInfo.pQueuePriorities = &queuePriority;
            // Push it onto our Queue configs
            queueCreateInfos.push_back(queueCreateInfo);
        }

        // Config - Device features
        VkPhysicalDeviceFeatures deviceFeatures = {};
        deviceFeatures.samplerAnisotropy = VK_TRUE;

        // Config - Core
        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos = queueCreateInfos.data();

        // Enable features and extensions
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();

        // [!] This might not really be necessary anymore because
        // device specific validation layers have been deprecated
        if (enableValidationLayers) {
            createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
            createInfo.ppEnabledLayerNames = validationLayers.data();
        } 
        else {
            createInfo.enabledLayerCount = 0;
        }

        // Instantiate our logical device
        if (vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device) != VK_SUCCESS) 
        {
            throw std::runtime_error("failed to create logical device!");
        }

        // Get a queue handle for each queue family
        vkGetDeviceQueue(_device, indices.graphicsFamily, 0, &_graphicsQueue);
        vkGetDeviceQueue(_device, indices.presentFamily, 0, &_presentQueue);
    }

    void EngineDevice::createCommandPool() {

        // Locate a Queue Family based on our definition of which queue we'd like (Graphics and Present, here)
        QueueFamilyIndices queueFamilyIndices = findPhysicalQueueFamilies();

        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
        poolInfo.flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(_device, &poolInfo, nullptr, &_commandPool) != VK_SUCCESS) 
        {
            throw std::runtime_error("[EngineDevice] Failed to create a command pool!");
        }
    }

    /**
    * Call to our window ref to create the window surface with GLFW
    */
    void EngineDevice::createSurface() { window.createWindowSurface(_instance, &_surface); }

    /*
     * Determine if there is a non-integrated card and
     * make sure it includes the necessary properties to support our application
     */
    bool EngineDevice::isDiscreteDeviceSuitable(VkPhysicalDevice device) 
    {

        // Look for available queue compatibilities within our device
        QueueFamilyIndices indices = findQueueFamilies(device);

        // Ensure required extensions are available
        bool extensionsSupported = checkDeviceExtensionSupport(device);

        bool swapChainAdequate = false;

        // Query for swapchain support so we can draw to our surface accordingly
        if (extensionsSupported) 
        {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        VkPhysicalDeviceFeatures supportedFeatures;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        return indices.isComplete() && 
            deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU 
            && extensionsSupported 
            && swapChainAdequate 
            && supportedFeatures.samplerAnisotropy;
    }

    /*
     * If there's no discrete device, we'll use this to search for integrated graphics
     * and make sure it includes the necessary properties to support our application
     */
    bool EngineDevice::isAnyDeviceSuitable(VkPhysicalDevice device)
    {

        // Look for available queue compatibilities within our device
        QueueFamilyIndices indices = findQueueFamilies(device);

        // Ensure required extensions are available
        bool extensionsSupported = checkDeviceExtensionSupport(device);

        bool swapChainAdequate = false;
        // Query for swapchain support so we can draw to our surface accordingly
        if (extensionsSupported)
        {
            SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);
            swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
        }

        VkPhysicalDeviceFeatures supportedFeatures;
        vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);

        return indices.isComplete()
            && extensionsSupported
            && swapChainAdequate
            && supportedFeatures.samplerAnisotropy;
    }

    /**
     *  Populate the struct which describes the details about the debug messenger.
     */
    void EngineDevice::populateDebugMessengerCreateInfo(
        VkDebugUtilsMessengerCreateInfoEXT &createInfo
    ) {
      createInfo = {};
      createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
      // Bitwise OR enables all of the included
      createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      createInfo.pfnUserCallback = debugCallback;
      createInfo.pUserData = nullptr;  // Optional
    }

    void EngineDevice::setupDebugMessenger() {

        if (!enableValidationLayers) return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo;

        // Apply configurations to the struct
        populateDebugMessengerCreateInfo(createInfo);

        if (CreateDebugUtilsMessengerEXT(_instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
            throw std::runtime_error("failed to set up debug messenger!");
        }
    }

    /**
     *  Check if all of the requested layers are available
     */
    bool EngineDevice::checkValidationLayerSupport() 
    {

        uint32_t layerCount;

        // List all of the available layers.
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        // Generate an array to store the names of our available validation layers in and acquire them
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        // Check and see if the validation layers we specified are supported
        for (const char *layerName : validationLayers) 
        {
            bool layerFound = false;

            for (const auto &layerProperties : availableLayers) 
            {
                if (strcmp(layerName, layerProperties.layerName) == 0) 
                {
                    layerFound = true;
                    break;
                }
            }

            if (!layerFound) 
            {
                std::cout << "[Error - Validation Layer Not Found] " << layerName << std::endl;
                return false;
            }
        }

        return true;
    }

    /**
     *  [Validators]
     *  Acquire a list of required extensions
     */
    std::vector<const char *> EngineDevice::getRequiredExtensions() 
    {

        uint32_t glfwExtensionCount = 0;
        const char **glfwExtensions;

        // Collect a list of our required extensions
        glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        std::vector<const char *> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        if (enableValidationLayers) 
        {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // == "VK_EXT_debug_utils"
        }

        return extensions;
    }

    /**
    * [Validators]
    * Retrieve a list of available extensions on this system
    */
    void EngineDevice::hasGflwRequiredInstanceExtensions() 
    {
        uint32_t extensionCount = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);

        // Hold the extension details
        std::vector<VkExtensionProperties> extensions(extensionCount);

        // Query the extension details
        vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

        // Print to terminal
        std::cout << "available extensions:" << std::endl;
        std::unordered_set<std::string> available;
        for (const auto &extension : extensions) 
        {
            std::cout << "\t" << extension.extensionName << std::endl;
            available.insert(extension.extensionName);
        }

        // Print to terminal
        std::cout << "required extensions:" << std::endl;
        auto requiredExtensions = getRequiredExtensions();
        for (const auto &required : requiredExtensions) 
        {
            std::cout << "\t" << required << std::endl;
            if (available.find(required) == available.end()) 
            {
                throw std::runtime_error("Missing required glfw extension");
            }
        }
    }

    /*
    * [Validators]
    \*/
    bool EngineDevice::checkDeviceExtensionSupport(VkPhysicalDevice device) 
    {

        uint32_t extensionCount;
        // Enumerate our devices extension properties
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        // Collect our device extensions now that we know how many we have
        vkEnumerateDeviceExtensionProperties(
            device,
            nullptr,
            &extensionCount,
            availableExtensions.data()
        );

        std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

        // Remove names as we find them
        for (const auto &extension : availableExtensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        // If all requried extensions were found, we should be left with no data
        return requiredExtensions.empty();
    }

    /**
    * Figure out the queue families supported by the device.
    * In this implementation we require Graphics and Presentation queues
    */
    QueueFamilyIndices EngineDevice::findQueueFamilies(VkPhysicalDevice device) 
    {

        QueueFamilyIndices indices;

        // Enumerate each queue available on our device
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        // Extract available queue families from our GPU driver as a vector of struct VkQueueFamilyProperties
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        // By locating a queue for graphics and presentation we can ensure that our device is suitable
        int i = 0;
        for (const auto &queueFamily : queueFamilies) {

            // Find a graphics queue. We need at least 1 queue family that supports VK_QUEUE_GRAPHICS_BIT
            if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) 
            {
                indices.graphicsFamily = i;
                indices.graphicsFamilyHasValue = true;
            }

            //  Look for a queue family that has the capability of presenting to our window surface
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, _surface, &presentSupport);

            // Find a presentation queue. This could very well be the same thing as the graphics queue
            if (queueFamily.queueCount > 0 && presentSupport) 
            {
                indices.presentFamily = i;
                indices.presentFamilyHasValue = true;
            }

            // Do we have a queue for each family of processes?
            if (indices.isComplete()) 
            {
                break;
            }

            i++;
        }

      return indices;
    }

    /*
     * Validate the capabilities of our device for swapchain support.
     * Returns our swapchain details
     */
    SwapChainSupportDetails EngineDevice::querySwapChainSupport(VkPhysicalDevice device) 
    {

        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, _surface, &details.capabilities);

        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, nullptr);

        if (formatCount != 0) 
        {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, _surface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, _surface, &presentModeCount, nullptr);

        if (presentModeCount != 0) 
        {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(
                device,
                _surface,
                &presentModeCount,
                details.presentModes.data()
            );
        }

        return details;
    }

    VkFormat EngineDevice::findSupportedFormat(const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features) 
    {
        for (VkFormat format : candidates) 
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(_physicalDevice, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) 
            {
                return format;
            } 
            else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) 
            {
                return format;
            }
        }
        throw std::runtime_error("failed to find supported format!");
    }

    uint32_t EngineDevice::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) 
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(_physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) 
        {
            if (
                (typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties
            ) {
                return i;
            }
        }

      throw std::runtime_error("failed to find suitable memory type!");
    }

    /*
    * @function void EngineDevice::createBuffer
    * Bind a buffer to specific GPU memory by
    * allocating a mapped region
    */
    void EngineDevice::createBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer &buffer,
        VkDeviceMemory &bufferMemory
    ) {
        // Create the Buffer given the provided information
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        // Create the buffer
        if (vkCreateBuffer(_device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) 
        {
            throw std::runtime_error("Device failed to create buffer!");
        }

        // Query the buffer's memory requirements so we can allocate according to proper size & properties
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(_device, buffer, &memRequirements);

        // Allocate the buffer's memory. Yet another info/action setup convention
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        // Allocate memory object. bufferMemory will now contain information describing the allocated memory
        if (vkAllocateMemory(_device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) 
        {
            throw std::runtime_error("Device failed to allocate buffer memory!");
        }

        // Bind the buffer to device memory. NO SPARSE MEMORY BINDING FLAGS
        vkBindBufferMemory(_device, buffer, bufferMemory, 0);
    }

    /*
    * @function beginSingleTimeCommands(void)
    * Allocate a command buffer in memory and return a pointer to it
    */
    VkCommandBuffer EngineDevice::beginSingleTimeCommands() {

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = _commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        vkAllocateCommandBuffers(_device, &allocInfo, &commandBuffer);
        /*
        * (Above) vkAllocateCommandBuffers
        * _device - the LOGICAL device that owns the command pool
        * allocInfo - describing parameters of the allocation
        * commandBuffer -  Pointer to array of cmd buffers. Must be at least the length specified by the 
        * commandBufferCount member of pAllocateInfo. Each allocated command buffer begins in the initial state.
        */

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);
        return commandBuffer;
    }

    void EngineDevice::endSingleTimeCommands(VkCommandBuffer commandBuffer) 
    {
        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(_graphicsQueue);

        vkFreeCommandBuffers(_device, _commandPool, 1, &commandBuffer);
    }

    void EngineDevice::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size) 
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferCopy copyRegion{};
        //copyRegion.srcOffset = 0;  // Optional
        //copyRegion.dstOffset = 0;  // Optional
        copyRegion.size = size;
        vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

        endSingleTimeCommands(commandBuffer);
    }

    void EngineDevice::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount) 
    {
        VkCommandBuffer commandBuffer = beginSingleTimeCommands();

        VkBufferImageCopy region{};
        region.bufferOffset = 0;        // Offset at which the pixels start
        region.bufferRowLength = 0;     // Data -
        region.bufferImageHeight = 0;   // - dimensionality. 0 means there is no padding. Tightly packed

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = layerCount;    // Typically 1 for now

        region.imageOffset = {0, 0, 0};        // These next 2 properties tell vulkan which part of the image we want to copy
        region.imageExtent = {width, height, 1};            

        vkCmdCopyBufferToImage(
            commandBuffer,
            buffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,   // Which layout the image is currently using
            1,
            &region);

        /*
            Right now we're only copying one chunk of pixels to the whole image, but it's possible 
            it's possible to specify an array of VkBufferImageCopy to perform many different 
            copies from this buffer to the image in one operation.
        */

        endSingleTimeCommands(commandBuffer);
    }

    /*
    * Reserve memory based on provided property(ies)
    */
    void EngineDevice::createImageWithInfo(
        const VkImageCreateInfo &imageInfo,
        VkMemoryPropertyFlags properties,
        VkImage &image,
        VkDeviceMemory &imageMemory
    ) {
        if (vkCreateImage(_device, &imageInfo, nullptr, &image) != VK_SUCCESS) 
        {
            throw std::runtime_error("failed to create image!");
        }

        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(_device, image, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

        if (vkAllocateMemory(_device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) 
        {
            throw std::runtime_error("failed to allocate image memory!");
        }

        if (vkBindImageMemory(_device, image, imageMemory, 0) != VK_SUCCESS) 
        {
            throw std::runtime_error("failed to bind image memory!");
        }
    }

}  // namespace aveng