#pragma once
#include <cstdint>
#include "graphics_enums.h"
#include <vulkan/vulkan_core.h>
#include <AMD/vk_mem_alloc.h>

namespace aveng  {

    VkSamplerCreateInfo defaultSampler{
        
    };

     typedef uint32_t ResourceHandle;

    struct Buffer {

        VkBuffer                        vk_buffer;
        VmaAllocation                   vma_allocation;
        VkDeviceMemory                  vk_device_memory;
        VkDeviceSize                    vk_device_size;

        VkBufferUsageFlags              type_flags = 0;
        ResourceTypes::Enum             usage = ResourceTypes::Immutable;
        uint32_t                             size = 0;
        uint32_t                             global_offset = 0;    // Offset into global constant, if dynamic

        BufferHandle                    handle;
        BufferHandle                    parent_buffer;

        const char* name = nullptr;

    }; // struct Buffer

    
    struct Texture {

        VkImage           vk_image;
        VkImageView       vk_image_view;
        VkFormat          vk_format;
        VkImageLayout     vk_image_layout;
        VmaAllocation     vma_allocation;

        uint16_t          width = 1;
        uint16_t          height = 1;
        uint16_t          depth = 1;
        uint8_t                mipmaps = 1;
        uint8_t                flags = 0;

        TextureHandle     handle;
        TextureType::Enum type = TextureType::Texture2D;

        Sampler* sampler = nullptr;

        const char* name = nullptr;

    }; // struct Texture

    //
    //
    struct Sampler {

        VkSampler                       vk_sampler;

        VkFilter                        min_filter = VK_FILTER_NEAREST;
        VkFilter                        mag_filter = VK_FILTER_NEAREST;
        VkSamplerMipmapMode             mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;

        VkSamplerAddressMode            address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode            address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode            address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        const char* name = nullptr;

    }; // struct Sampler

    //
    //
    struct BufferCreation {

        VkBufferUsageFlags              type_flags = 0;
        ResourceTypes::Enum             usage = ResourceTypes::Immutable;
        uint32_t                        size = 0;
        void* initial_data = nullptr;

        const char* name = nullptr;

        BufferCreation& reset();
        BufferCreation& set(VkBufferUsageFlags flags, ResourceTypes::Enum usage, uint32_t size);
        BufferCreation& set_data(void* data);
        BufferCreation& set_name(const char* name);

    }; // struct BufferCreation

    //
    //
    struct TextureCreation {

        void*       initial_data = nullptr;
        uint16_t    width = 1;
        uint16_t    height = 1;
        uint16_t    depth = 1;
        uint8_t     mipmaps = 1;
        uint8_t     flags = 0;    // TextureFlags bitmasks

        VkFormat           format = VK_FORMAT_UNDEFINED;
        TextureType::Enum  type = TextureType::Texture2D;

        const char* name = nullptr;

        TextureCreation& set_size(uint16_t width, uint16_t height, uint16_t depth);
        TextureCreation& set_flags(uint8_t mipmaps, uint8_t flags);
        TextureCreation& set_format_type(VkFormat format, TextureType::Enum type);
        TextureCreation& set_name(const char* name);
        TextureCreation& set_data(void* data);

    }; // struct TextureCreation

    //
    //
    struct SamplerCreation {

        VkFilter              min_filter = VK_FILTER_NEAREST;
        VkFilter              mag_filter = VK_FILTER_NEAREST;
        VkSamplerMipmapMode   mip_filter = VK_SAMPLER_MIPMAP_MODE_NEAREST;
                              
        VkSamplerAddressMode  address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode  address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        VkSamplerAddressMode  address_mode_w = VK_SAMPLER_ADDRESS_MODE_REPEAT;

        const char* name = nullptr;

        SamplerCreation& set_min_mag_mip(VkFilter min, VkFilter mag, VkSamplerMipmapMode mip);
        SamplerCreation& set_address_mode_u(VkSamplerAddressMode u);
        SamplerCreation& set_address_mode_uv(VkSamplerAddressMode u, VkSamplerAddressMode v);
        SamplerCreation& set_address_mode_uvw(VkSamplerAddressMode u, VkSamplerAddressMode v, VkSamplerAddressMode w);
        SamplerCreation& set_name(const char* name);

    }; // struct SamplerCreation

}