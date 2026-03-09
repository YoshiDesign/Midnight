#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <span>

#include <vulkan/vulkan_core.h>
#include <AMD/vk_mem_alloc.h>
#include <assimp/texture.h>

#include "CoreVK/Resources/platform.h"

namespace aveng {

    using TextureHandle = uint32_t;		    // Primary lookup resource
    using TextureAssetID = uint64_t;	    // Content identity
    static constexpr TextureHandle kInvalidTextureHandle = 0;

    enum class TextureState : uint8_t {
        Unloaded,
        Resident,
        Failed
    };

    // While loading models we create these to detect and pass along embedded textures.
    struct GltfImageRef {
        bool embedded = false;  // False will fallback to load from filesystem
        std::string uri;                     // normalized external path if not embedded
        std::vector<std::byte> embeddedData; // compressed bytes if embedded
        std::string debugName;
        bool srgb = true;
    };

    struct TextureAssetKey {
        std::string value;

        bool operator==(const TextureAssetKey& other) const noexcept {
            return value == other.value;
        }
    };

    struct TextureSlot {
        TextureHandle handle = kInvalidTextureHandle;
        TextureAssetKey assetKey;

        uint32_t bindlessIndex = k_invalid_index;

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipCount = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        bool srgb = false;

        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        TextureState state = TextureState::Unloaded;
        std::string debugName;
    };

    struct MTextureStagingBuffer {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingBufferAlloc = VK_NULL_HANDLE;
    };

    struct MSamplerInfo {
        VkFilter magFilter = VK_FILTER_LINEAR;
        VkFilter minFilter = VK_FILTER_LINEAR;
        VkSamplerAddressMode addressU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode addressW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    };

    struct MipSlice {
        uint32_t mipLevel = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        size_t offset = 0;
        size_t size = 0;
    };

    // Ephemeral request to create a texture
    struct TextureCreateRequest {
        TextureAssetKey assetKey;
        std::string debugName;

        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t mipCount = 1;
        VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
        VkImageType imageType = VK_IMAGE_TYPE_2D;
        bool srgb = false;

        aiTexel* assimp_data;
        MSamplerInfo samplerInfo{};

        std::span<const std::byte> pixelBlob;
        std::vector<MipSlice> mipSlices;
    };

    // Handle - DEPRECATED
    struct TextureRef {
        TextureHandle index;         // handle.index for index of registry slots
        const char* key;        // naive approach to uniqueness for now
        int key_hash;           // For future reference
    };

}