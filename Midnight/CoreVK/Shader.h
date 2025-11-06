/* Vulkan shader */
#pragma once

#include <string>
#include <vulkan/vulkan.h>
namespace aveng {
    class Shader {
    public:
        static VkShaderModule loadShader(VkDevice device, std::string shaderFileName);
        static void cleanup(VkDevice device, VkShaderModule module);
    };
}