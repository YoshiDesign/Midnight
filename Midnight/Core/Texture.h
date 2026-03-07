#pragma once

#include <string>
#include <vector>
#include <cstdint>

#include <vulkan/vulkan_core.h>
#include <AMD/vk_mem_alloc.h>
#include <assimp/texture.h>

#include "CoreVK/VkRenderData.h"

namespace aveng {

    class EngineDevice;

/// /// /// 
    struct TextureSlot {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr; 
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        // TODO: format, extent, mipCount, debugName, etc.
        bool alive = false;
    };

    struct TextureRegistry {
        std::vector<TextureSlot> slots; // Slot index == descriptor index for bindless textures
        std::vector<uint32_t> freeList; // indices into slots
    };
/// /// ///

	struct VkTextureStagingBuffer {
		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VmaAllocation stagingBufferAlloc = VK_NULL_HANDLE;
	};

    class Texture {
    public:

        static bool loadTexture(EngineDevice& engineDevice, const VkRenderData& renderData, VkTextureData& texData, std::string textureFilename,
            bool generateMipmaps = true, bool flipImage = false);

        static bool loadTexture(EngineDevice& engineDevice, const VkRenderData& renderData, VkTextureData& texData, std::string textureName,
            aiTexel* textureData, int width, int height, bool generateMipmaps = true, bool flipImage = false);

        static void cleanup(EngineDevice& engineDevice, const VkRenderData& renderData, VkTextureData& texData);

    private:
        static bool uploadToGPU(EngineDevice& engineDevice, const VkRenderData& renderData, VkTextureData& texData, VkTextureStagingBuffer& stagingData,
            uint32_t width, uint32_t height, bool generateMipmaps, uint32_t mipmapLevels);
    };
}