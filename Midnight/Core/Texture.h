/* Vulkan texture */
#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <cstdint>

#include <assimp/texture.h>

#include "CoreVK/VkRenderData.h"

namespace aveng {
    struct VkTextureStagingBuffer {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingBufferAlloc = VK_NULL_HANDLE;
    };

    class Texture {
    public:
        static bool loadTexture(EngineDevice& engineDevice, VkRenderData& renderData, VkTextureData& texData, std::string textureFilename,
            bool generateMipmaps = true, bool flipImage = false);
        static bool loadTexture(EngineDevice& engineDevice, VkRenderData& renderData, VkTextureData& texData, std::string textureName,
            aiTexel* textureData, int width, int height, bool generateMipmaps = true, bool flipImage = false);

        static void cleanup(EngineDevice& engineDevice, VkRenderData& renderData, VkTextureData& texData);

    private:
        static bool uploadToGPU(EngineDevice& engineDevice, VkRenderData& renderData, VkTextureData& texData, VkTextureStagingBuffer& stagingData,
            uint32_t width, uint32_t height, bool generateMipmaps, uint32_t mipmapLevels);
    };
}