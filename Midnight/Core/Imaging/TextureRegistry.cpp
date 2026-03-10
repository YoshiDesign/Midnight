#include <cassert>
#include "TextureRegistry.h"

namespace aveng {

    //
    std::vector<TextureHandle> TextureRegistry::getOrCreateMany(std::vector<TextureAssetKey>& keys, std::vector<TextureCreateRequest>& reqs, ITextureSource& source, const int frameIndex) {

        // Expect parallel arrays
        if (keys.size() != reqs.size()) {
            assert(false && "vector sizes are not equivalent");
            return {};
        }

        std::vector<TextureHandle> new_handles{};

        for (size_t i = 0; i < keys.size(); ++i) {
            const auto& key = keys[i];

            auto it = m_assetToHandle.find(key);
            if (it != m_assetToHandle.end()) {
                continue;
            }

            TextureHandle h = getOrCreate(keys[i], source, reqs[i], frameIndex);
            new_handles.push_back(h);
        }

        return new_handles;
    }

    //
    TextureHandle TextureRegistry::getOrCreate(const TextureAssetKey& key, ITextureSource& source, TextureCreateRequest& req, const int frameIndex) {
        auto it = m_assetToHandle.find(key);
        if (it != m_assetToHandle.end()) {
            return it->second;
        }

        // Load bytes (pixelBlob) and determine mip levels
        if (!source.loadTexture(key, req)) {
            return kInvalidTextureHandle;
        }

        // Bookkeeping
        uint32_t next_descriptor_index = m_assetToHandle.size();

        // Upload to GPU
        TextureHandle handle = m_textureSystem.createTexture(source, req, next_descriptor_index, frameIndex);

        m_assetToHandle.emplace(key, handle);
        
        return handle;
    }

    // Correctness for `const TextureRegistry`
    const TextureSlot* TextureRegistry::get(TextureHandle handle) const {
        return m_textureSystem.getSlot(handle);
    }

    TextureSlot* TextureRegistry::get(TextureHandle handle) {
        return m_textureSystem.getSlot(handle);
    }

}