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

    struct MidTextureStagingBuffer {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingBufferAlloc = VK_NULL_HANDLE;
    };

    /// /// /// Trivially Copyable
    struct TextureSlot {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        uint32_t mipLevels;
        bool alive = false;
    };

    // For Future Use
    //VkImageCreateInfo just_for_reference;
    //struct ImageCreation {
    //    VkFormat format;        // e.g. VK_FORMAT_R8G8B8A8_SRGB or UNORM
    //    VkImageType imageType;  // e.g. VK_IMAGE_TYPE_2D
    //};

    // Handle
    struct TextureRef {
        size_t slotIdx;         // handle.index for index of registry slots
        const char* key;        // naive approach to uniqueness for now
        int key_hash;           // For future reference
    };

    // Owned by the Renderer
    struct TextureRegistry {
        std::vector<TextureSlot> slots; // Slot index == descriptor index for bindless textures
        std::unordered_map<std::string, TextureRef> references; // Texture name/identifier and index. Just for dedupe
        std::vector<uint32_t> freeList; // indices into slots
    };
    /// /// ///

}