#include "MidnightTextureSystem.h"
#include "CoreVK/VkRenderData.h"
#include "CoreVK/EngineDevice.h"

namespace aveng {
    MidnightTextureSystem::MidnightTextureSystem(EngineDevice& _engineDevice, VkRenderData _renderData)
        : engineDevice_{ _engineDevice }, renderData_{ _renderData }
    {
    }
    TextureHandle MidnightTextureSystem::createTexture(const TextureCreateRequest& req, const int frameIndex) {

        /* TODO */

        TextureSlot slot{};
        slot.handle = m_nextHandle++;
        slot.assetKey = req.assetKey;
        slot.width = req.width;
        slot.height = req.height;
        slot.mipCount = req.mipCount;
        slot.format = req.format;
        slot.srgb = req.srgb;
        slot.debugName = req.debugName;

        slot.bindlessIndex = allocateBindlessSlot();
        slot.sampler = getOrCreateSampler(req.samplerInfo);

        uploadTexturePixels(req, slot);
        updateBindlessDescriptor(slot.bindlessIndex, slot, frameIndex);

        slot.state = TextureState::Resident;

        if (m_records.size() < slot.handle) {
            m_records.resize(slot.handle);
        }
        m_records[slot.handle - 1] = slot;

        return slot.handle;
    }

    void MidnightTextureSystem::uploadTexturePixels(const TextureCreateRequest& req, TextureSlot& rec)
    {
    }

    /* TODO : Now would be a great time to simply keep the frameIndex updated in one place (VkRenderData) */
    void MidnightTextureSystem::updateBindlessDescriptor(uint32_t slotIdx, const TextureSlot& tex, const int frameIndex)
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = tex.imageView;
        imageInfo.sampler = tex.sampler;

        VkWriteDescriptorSet write{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
        };
        write.dstSet = renderData_.rdAvengBindlessDescriptorSets[frameIndex];
        write.dstBinding = BINDLESS_TEXTURE_BINDING_0;
        write.dstArrayElement = slotIdx;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(engineDevice_.device(), 1, &write, 0, nullptr);

    }

}