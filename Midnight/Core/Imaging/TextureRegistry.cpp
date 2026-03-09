#include "TextureRegistry.h"

namespace aveng {

    /* TODO */

    std::vector<TextureHandle> TextureRegistry::getOrCreateMany(std::vector<const TextureAssetKey>& keys, ITextureSource& source, const int frameIndex) {
    
        for (const auto& key : keys) {
            auto it = m_assetToHandle.find(key);
            if (it != m_assetToHandle.end()) {
                return {}; // TODO - Not caring right now.
            }
        }

        std::vector<TextureCreateRequest> reqs{};
        std::vector<TextureHandle> new_handles{};
        reqs.reserve(keys.size());
        new_handles.reserve(keys.size());

        for (auto& key : keys) {
            static int i = 0;
            if (!source.loadTexture(key, reqs[i])) {
                new_handles.push_back(kInvalidTextureHandle);
            }

            TextureHandle handle = m_textureSystem.createTexture(reqs[i]);

            m_assetToHandle[key] = handle;
            new_handles.push_back(handle);
        }

        return new_handles;
    }

    TextureHandle TextureRegistry::getOrCreate(const TextureAssetKey& key, ITextureSource& source, TextureCreateRequest& req, const int frameIndex) {
        auto it = m_assetToHandle.find(key);
        if (it != m_assetToHandle.end()) {
            return it->second;
        }

        // Complete request struct - load bytes into req.pixelBlob
        if (!source.loadTexture(key, req)) {
            return kInvalidTextureHandle;
        }

        // Upload to GPU
        TextureHandle handle = m_textureSystem.createTexture(req, frameIndex);

        m_assetToHandle[key] = handle;
        
        return handle;
    }

    // Correctness for `const TextureRegistry`
    const TextureSlot* TextureRegistry::get(TextureHandle handle) const {
    
    }

    TextureSlot* TextureRegistry::get(TextureHandle handle) {
    
    }

}