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
            std::vector<const TextureAssetKey>& keys, 
            ITextureSource& source, 
            const int frameIndex);

        TextureHandle getOrCreate(const TextureAssetKey& key, ITextureSource& source, TextureCreateRequest& req, const int frameIndex);

        const TextureSlot* get(TextureHandle handle) const;
        TextureSlot* get(TextureHandle handle);

    private:

        MidnightTextureSystem& m_textureSystem;
        std::unordered_map<TextureAssetKey, TextureHandle> m_assetToHandle; // Look up the handle to retrieve the slot
        std::vector<TextureSlot> m_records; // index by handle-1 if you like

	};
}