#include "MidnightTextureSystem.h"
#include "CoreVK/VkRenderData.h"
#include "CoreVK/EngineDevice.h"
#include "Utils/Logger.h"

namespace aveng {
    MidnightTextureSystem::MidnightTextureSystem(EngineDevice& _engineDevice, VkRenderData _renderData)
        : engineDevice_{ _engineDevice }, renderData_{ _renderData }
    {
        
    }
    TextureHandle MidnightTextureSystem::createTexture(ITextureSource& source, TextureCreateRequest& req, uint32_t next_descriptor_index, const int frameIndex) {

        /* We have the image blob at this point req.pixelBlob */

        TextureSlot slot{};
        slot.handle = m_nextHandle++;
        slot.assetKey = req.assetKey;
        slot.width = req.width;
        slot.height = req.height;
        slot.size = req.size;
        slot.mipLevels = req.mipLevels;
        slot.format = req.format;
        slot.srgb = req.srgb; // TODO 
        slot.debugName = req.debugName;

        // TODO - Move these into a supported feature/properties cache
        VkPhysicalDeviceFeatures supportedFeatures{};
        vkGetPhysicalDeviceFeatures(engineDevice_.physicalDevice(), &supportedFeatures);
        req.samplerInfo.anisotropyAvailable = supportedFeatures.samplerAnisotropy;

        VkPhysicalDeviceProperties physProperties{};
        vkGetPhysicalDeviceProperties(engineDevice_.physicalDevice(), &physProperties);
        req.samplerInfo.maxAnisotropy = physProperties.limits.maxSamplerAnisotropy;

        // Create Sampler in slot.sampler
        getOrCreateSampler(req.samplerInfo, slot);

        if (uploadToGPU(slot, source, req.pixelBlob)) {

            slot.bindlessIndex = next_descriptor_index;

            updateBindlessDescriptor(slot.bindlessIndex, slot, frameIndex);

            slot.state = TextureState::Resident;

            // TODO - 
            if (m_records.size() < slot.handle) {
                m_records.resize(slot.handle); 
            }

            m_records[slot.handle - 1] = slot;

            return slot.handle;

        }

        // Destroy the components
        slot.state = TextureState::Failed;
        vkDestroySampler(engineDevice_.device(), slot.sampler, nullptr);
        vkDestroyImageView(engineDevice_.device(), slot.imageView, nullptr);
        vmaDestroyImage(engineDevice_.allocator(), slot.image, slot.imageAlloc);
        return k_invalid_index;

    }

    const TextureSlot* MidnightTextureSystem::getSlot(TextureHandle handle) const
    {
        return &m_records[handle];
    }

    TextureSlot* MidnightTextureSystem::getSlot(TextureHandle handle)
    {
        return &m_records[handle];
    }

    // Note: lots of redundant information between req and slot, but its harmless for now.
    bool MidnightTextureSystem::uploadToGPU(TextureSlot& slot, ITextureSource& source, unsigned char* pixelBlob)
    {
        /* staging buffer */
        VkBufferCreateInfo stagingBufferInfo{};
        stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufferInfo.size = slot.size;
        stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VkBuffer stagingBuffer;
        VmaAllocation stagingBufferAlloc;

        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;

        VkResult result = vmaCreateBuffer(engineDevice_.allocator(), &stagingBufferInfo, &stagingAllocInfo, &stagingBuffer, &stagingBufferAlloc, nullptr);
        if (result != VK_SUCCESS) {
            slot.state = TextureState::Failed;
            std::printf("%s error: could not allocate texture staging buffer via VMA (error: %i)\n", __FUNCTION__, result);
            source.destroyTexture(pixelBlob);
            return false;
        }

        void* uploadData;
        result = vmaMapMemory(engineDevice_.allocator(), stagingBufferAlloc, &uploadData);
        if (result != VK_SUCCESS) {
            slot.state = TextureState::Failed;
            std::printf("%s error: could not map texture memory (error: %i)\n", __FUNCTION__, result);
            source.destroyTexture(pixelBlob);
            return false;
        }
        std::memcpy(uploadData, pixelBlob, slot.size);
        vmaUnmapMemory(engineDevice_.allocator(), stagingBufferAlloc);
        vmaFlushAllocation(engineDevice_.allocator(), stagingBufferAlloc, 0, slot.size);

        source.destroyTexture(pixelBlob);

        /* upload */
        VkCommandBuffer uploadCommandBuffer = engineDevice_.createSingleShotBuffer();

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = slot.width;
        imageInfo.extent.height = slot.height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = slot.mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo imageAllocInfo{};
        imageAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        result = vmaCreateImage(engineDevice_.allocator(), &imageInfo, &imageAllocInfo, &slot.image, &slot.imageAlloc, nullptr);
        if (result != VK_SUCCESS) {
            slot.state = TextureState::Failed;
            std::printf("%s error: could not allocate texture image via VMA (error: %i)\n", __FUNCTION__, result);
            return false;
        }

        VkImageSubresourceRange stagingBufferRange{};
        stagingBufferRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        stagingBufferRange.baseMipLevel = 0;
        stagingBufferRange.levelCount = slot.mipLevels;
        stagingBufferRange.baseArrayLayer = 0;
        stagingBufferRange.layerCount = 1;

        /* 1st barrier, undefined to transfer optimal */
        VkImageMemoryBarrier stagingBufferTransferBarrier{};
        stagingBufferTransferBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        stagingBufferTransferBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // HmMmmmMmMm
        stagingBufferTransferBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        stagingBufferTransferBarrier.image = slot.image;
        stagingBufferTransferBarrier.subresourceRange = stagingBufferRange;
        stagingBufferTransferBarrier.srcAccessMask = 0;
        stagingBufferTransferBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        VkExtent3D textureExtent{};
        textureExtent.width = slot.width;
        textureExtent.height = slot.height;
        textureExtent.depth = 1;

        VkBufferImageCopy stagingBufferCopy{};
        stagingBufferCopy.bufferOffset = 0;
        stagingBufferCopy.bufferRowLength = 0;
        stagingBufferCopy.bufferImageHeight = 0;
        stagingBufferCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        stagingBufferCopy.imageSubresource.mipLevel = 0;
        stagingBufferCopy.imageSubresource.baseArrayLayer = 0;
        stagingBufferCopy.imageSubresource.layerCount = 1;
        stagingBufferCopy.imageExtent = textureExtent;

        /* 2nd barrier, transfer optimal to shader optimal */
        VkImageMemoryBarrier stagingBufferShaderBarrier{};
        stagingBufferShaderBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        stagingBufferShaderBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        if (slot.mipLevels > 1) {
            /* VkCmdBlit() requires the original image to be in TRANSFER_DST_OPTIMAL format */
            stagingBufferShaderBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        else {
            stagingBufferShaderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        stagingBufferShaderBarrier.image = slot.image;
        stagingBufferShaderBarrier.subresourceRange = stagingBufferRange;
        stagingBufferShaderBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        stagingBufferShaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &stagingBufferTransferBarrier);
        vkCmdCopyBufferToImage(uploadCommandBuffer, stagingBuffer, slot.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &stagingBufferCopy);
        vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &stagingBufferShaderBarrier);

        /* generate mipmap blit commands */
        if (slot.mipLevels > 1) {
            VkImageSubresourceRange blitRange{};
            blitRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitRange.baseMipLevel = 0;
            blitRange.levelCount = 1;
            blitRange.baseArrayLayer = 0;
            blitRange.layerCount = 1;

            /* 1st barrier, we need to transfer to src optimal for the blit */
            VkImageMemoryBarrier firstBarrier{};
            firstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            firstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            firstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            firstBarrier.image = slot.image;
            firstBarrier.subresourceRange = blitRange;
            firstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            firstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            /* 2nd barrier -> transfer to shader optimal */
            VkImageMemoryBarrier secondBarrier{};
            secondBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            secondBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            secondBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            secondBarrier.image = slot.image;
            secondBarrier.subresourceRange = blitRange;
            secondBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            secondBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            VkImageBlit mipBlit{};
            mipBlit.srcOffsets[0] = { 0, 0, 0 };
            mipBlit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mipBlit.srcSubresource.baseArrayLayer = 0;
            mipBlit.srcSubresource.layerCount = 1;

            mipBlit.dstOffsets[0] = { 0, 0, 0 };
            mipBlit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            mipBlit.dstSubresource.baseArrayLayer = 0;
            mipBlit.dstSubresource.layerCount = 1;

            int32_t mipWidth = slot.width;
            int32_t mipHeight = slot.height;

            for (int i = 1; i < slot.mipLevels; ++i) {
                mipBlit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
                mipBlit.srcSubresource.mipLevel = i - 1;

                mipBlit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
                mipBlit.dstSubresource.mipLevel = i;

                firstBarrier.subresourceRange.baseMipLevel = i - 1;
                vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &firstBarrier);

                vkCmdBlitImage(uploadCommandBuffer,
                    slot.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    slot.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    1, &mipBlit, VK_FILTER_LINEAR);

                secondBarrier.subresourceRange.baseMipLevel = i - 1;
                vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &secondBarrier);

                if (mipWidth > 1) {
                    mipWidth /= 2;
                }
                if (mipHeight > 1) {
                    mipHeight /= 2;
                }

                // std::printf("%s: created level %i with width %i and height %i\n", __FUNCTION__, i, mipWidth, mipHeight);
            }

            VkImageMemoryBarrier lastBarrier{};
            lastBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            lastBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            lastBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            lastBarrier.image = slot.image;
            lastBarrier.subresourceRange = blitRange;
            lastBarrier.subresourceRange.baseMipLevel = slot.mipLevels - 1;
            lastBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            lastBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &lastBarrier);
        }

        bool commandResult = engineDevice_.submitSingleShotBuffer(uploadCommandBuffer);
        vmaDestroyBuffer(engineDevice_.allocator(), stagingBuffer, stagingBufferAlloc);

        if (!commandResult) {
            std::printf("%s error: could not submit texture transfer commands\n", __FUNCTION__);
            return false;
        }

        /* image view and sampler */
        VkImageViewCreateInfo texViewInfo{};
        texViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        texViewInfo.image = slot.image;
        texViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        texViewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        texViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        texViewInfo.subresourceRange.baseMipLevel = 0;
        texViewInfo.subresourceRange.levelCount = slot.mipLevels;
        texViewInfo.subresourceRange.baseArrayLayer = 0;
        texViewInfo.subresourceRange.layerCount = 1;

        result = vkCreateImageView(engineDevice_.device(), &texViewInfo, nullptr, &slot.imageView);
        if (result != VK_SUCCESS) {
            slot.state = TextureState::Failed;
            std::printf("%s error: could not create image view for texture\n", __FUNCTION__);
            return false;
        }

        return true;
    }

    void MidnightTextureSystem::getOrCreateSampler(const MSamplerInfo& desc, TextureSlot& slot)
    {
        VkSamplerCreateInfo texSamplerInfo{};
        texSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        texSamplerInfo.magFilter = desc.magFilter;
        texSamplerInfo.minFilter = desc.minFilter;
        texSamplerInfo.addressModeU = desc.addressU;
        texSamplerInfo.addressModeV = desc.addressV;
        texSamplerInfo.addressModeW = desc.addressW;
        texSamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        texSamplerInfo.unnormalizedCoordinates = VK_FALSE;
        texSamplerInfo.compareEnable = VK_FALSE;
        texSamplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        texSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        texSamplerInfo.mipLodBias = 0.0f;
        texSamplerInfo.minLod = 0.0f;
        texSamplerInfo.maxLod = static_cast<float>(slot.mipLevels);
        texSamplerInfo.anisotropyEnable = desc.anisotropyAvailable;
        texSamplerInfo.maxAnisotropy = desc.maxAnisotropy;

        VkResult result = vkCreateSampler(engineDevice_.device(), &texSamplerInfo, nullptr, &slot.sampler);
        if (result != VK_SUCCESS) {
            slot.state = TextureState::Failed;
            std::printf("%s [[ error ]] could not create sampler for texture (error: %i)\n", __FUNCTION__, result);
        }
    }

    /* TODO : Now would be a great time to simply keep the frameIndex updated in one place (VkRenderData) */
    void MidnightTextureSystem::updateBindlessDescriptor(uint32_t slotIdx, const TextureSlot& slot, const int frameIndex)
    {
        Logger::log(3, "Loading texture into slot %d\n", slotIdx);
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = slot.imageView;
        imageInfo.sampler = slot.sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = renderData_.rdAvengBindlessDescriptorSets[frameIndex];
        write.dstBinding = BINDLESS_TEXTURE_BINDING_0;
        write.dstArrayElement = slotIdx;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        // Strange that this can't fail
        vkUpdateDescriptorSets(engineDevice_.device(), 1, &write, 0, nullptr);

    }

}