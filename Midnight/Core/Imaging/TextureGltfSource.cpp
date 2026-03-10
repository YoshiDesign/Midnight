#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "Core/Imaging/TextureGltfSource.h"

namespace aveng {

    /* Load texture, taking different resource sources into account */
	bool TextureGltfSource::loadTexture(const TextureAssetKey& key, TextureCreateRequest& outReq) {

        /* Embedded in model data */
        if (outReq.assimp_data != nullptr) {

            int texWidth;
            int texHeight;
            int numberOfChannels;
            uint32_t mipmapLevels = 1;

            /* good to know */
            // stbi_set_flip_vertically_on_load(flipImage);

            /* we use stbi to detect the in-memory format, but always request RGBA */
            // unsigned char* data = nullptr;
            if (outReq.height == 0) {
                outReq.pixelBlob = stbi_load_from_memory(reinterpret_cast<unsigned char*>(outReq.assimp_data), outReq.width, &texWidth, &texHeight, &numberOfChannels, STBI_rgb_alpha);
            }
            else {
                outReq.pixelBlob = stbi_load_from_memory(reinterpret_cast<unsigned char*>(outReq.assimp_data), outReq.width * outReq.height, &texWidth, &texHeight, &numberOfChannels, STBI_rgb_alpha);
            }

            if (!outReq.pixelBlob) {
                std::printf("%s error: could not load file '%s'\n", __FUNCTION__, key.value.c_str());
                stbi_image_free(outReq.pixelBlob);
                return false;
            }

            outReq.mipLevels += static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight))));
            outReq.size = texWidth * texHeight * 4; // Per the docs
            
            return true;
        }

	}

    void TextureGltfSource::destroyTexture(unsigned char* pixelBlob)
    {
        stbi_image_free(pixelBlob);
    }

}