#pragma once
#include <cstdint>
#include "graphics_enums.h"
#include <vulkan/vulkan_core.h>
#include <AMD/vk_mem_alloc.h>

namespace aveng {

    typedef uint32_t ResourceHandle;

    static const uint32_t k_invalid_index = 0xffffffff;

    struct BufferHandle {
        ResourceHandle index;
    };

    struct TextureHandle {
        ResourceHandle index;
    };

    struct ShaderStateHandle {
        ResourceHandle index;
    };

    struct SamplerHandle {
        ResourceHandle index;
    };

    struct DescriptorSetLayoutHandle {
        ResourceHandle index;
    };

    struct DescriptorSetHandle {
        ResourceHandle index;
    };

    struct PipelineHandle {
        ResourceHandle index;
    };

    struct RenderPassHandle {
        ResourceHandle index;
    };

    // Invalid handles
    static BufferHandle                 k_invalid_buffer    { k_invalid_index };
    static TextureHandle                k_invalid_texture   { k_invalid_index };
    static ShaderStateHandle            k_invalid_shader    { k_invalid_index };
    static SamplerHandle                k_invalid_sampler   { k_invalid_index };
    static DescriptorSetLayoutHandle    k_invalid_layout    { k_invalid_index };
    static DescriptorSetHandle          k_invalid_set       { k_invalid_index };
    static PipelineHandle               k_invalid_pipeline  { k_invalid_index };
    static RenderPassHandle             k_invalid_pass      { k_invalid_index };

    static const uint8_t                k_max_image_outputs = 8;                // Maximum number of images/render_targets/fbo attachments usable.
    static const uint8_t                k_max_descriptor_set_layouts = 8;       // Maximum number of layouts in the pipeline.
    static const uint8_t                k_max_shader_stages = 5;                // Maximum simultaneous shader stages. Applicable to all different type of pipelines.
    static const uint8_t                k_max_descriptors_per_set = 16;         // Maximum list elements for both descriptor set layout and descriptor sets.
    static const uint8_t                k_max_vertex_streams = 16;
    static const uint8_t                k_max_vertex_attributes = 16;
    static const uint32_t               k_submit_header_sentinel = 0xfefeb7ba;
    static const uint32_t               k_max_resource_deletions = 64;
          
    //
    //
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

    //
    //
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