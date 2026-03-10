#pragma once
#include <unordered_map>
#include <vector>
#include <cstddef>

#include "ITextureSource.h"
#include "CoreVK/Resources/MTexture.h"

namespace aveng {

    //struct GltfImageRef {
    //    bool embedded = false;
    //    std::string uri;                     // normalized external path or URI
    //    std::vector<std::byte> embeddedData; // compressed bytes if embedded
    //    std::string debugName;
    //    bool srgb = true;
    //};

	struct TextureGltfSource : public ITextureSource {

		bool loadTexture(const TextureAssetKey& key, TextureCreateRequest& outReq) override;
		void destroyTexture(unsigned char* pixelBlob);

        inline bool addRef(std::string keyname) {  }

    private:
        // Source scratch resource - <key, ref>
        // std::unordered_map<std::string, GltfImageRef> m_images;
	};

}