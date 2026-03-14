#pragma once
#include <unordered_map>
#include "CoreVK/Resources/MTexture.h"
#include "Core/Imaging/MidnightTextureSystem.h"
#include "Core/Imaging/ITextureSource.h"

namespace aveng {
    /* Owned by the Renderer */
	struct TextureRegistry {
        explicit TextureRegistry(MidnightTextureSystem& textureSystem)
            : m_textureSystem(textureSystem) {
        }

        std::vector<TextureHandle> getOrCreateMany(
            std::vector<TextureAssetKey>& keys, 
            std::vector<TextureCreateRequest>& reqs,
            ITextureSource& source, 
            const int frameIndex);

        TextureHandle getOrCreate(const TextureAssetKey& key, ITextureSource& source, TextureCreateRequest& req);

        // Fetch slots from the MidnightTextureSystem
        const TextureSlot* get(TextureHandle handle) const;
        TextureSlot* get(TextureHandle handle);

    private:

        /* In our current design, texture entries are never removed individually. So this is a simple approach to indexing */
        size_t nextTextureDescriptorIndex() const { return m_assetToHandle.size(); }

        /** 
         *  Lookup is: handle -> slot -> (descriptor_index) 
         *  A handle is not the value of its descriptor's index.
         */
        std::unordered_map <TextureAssetKey, TextureHandle, TextureAssetKeyHash> m_assetToHandle{}; // Look up the handle to retrieve the slot
        MidnightTextureSystem& m_textureSystem; // Keeper of the slots

	};
}