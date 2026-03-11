#pragma once
#include <unordered_map>
#include <vector>
#include <cstddef>

#include "ITextureSource.h"
#include "CoreVK/Resources/MTexture.h"

namespace aveng {

    /*
    * @Note
    * For glTF imports, Assimp often maps glTF baseColorTexture -> aiTextureType_DIFFUSE
    * for compatibility with older code paths. Later, Assimp added a dedicated texture
    * type aiTextureType_BASE_COLOR specifically to represent glTF's baseColor without pretending it's "diffuse"
    */

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