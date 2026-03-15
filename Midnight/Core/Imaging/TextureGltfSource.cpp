#include <cstring>
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "Core/Imaging/TextureGltfSource.h"

namespace aveng {

    /* Load texture, taking different resource sources into account */
	bool TextureGltfSource::loadTexture(const TextureAssetKey& key, TextureCreateRequest& outReq) {

        int texWidth;  // actual decoded width
        int texHeight; // actual decoded height

        /* Embedded in model data */
        if (outReq.assimp_data != nullptr) {

            /* good to know */
            // stbi_set_flip_vertically_on_load(flipImage);

            /* we use stbi to detect the in-memory format, but always request RGBA */
            if (outReq.pixelBlob == nullptr) {

                if (outReq.height == 0) {
                    outReq.pixelBlob = stbi_load_from_memory(reinterpret_cast<unsigned char*>(outReq.assimp_data), outReq.width, &texWidth, &texHeight, &outReq.numChannels, STBI_rgb_alpha);
                    outReq.width = texWidth;
                    outReq.height = texHeight;
                    
                }
                else {
                    outReq.pixelBlob = stbi_load_from_memory(reinterpret_cast<unsigned char*>(outReq.assimp_data), outReq.width * outReq.height, &texWidth, &texHeight, &outReq.numChannels, STBI_rgb_alpha);
                }

                outReq.size = texWidth * texHeight * 4;
            
            }

        }
        else {
            // load internally referenced textures from file
            outReq.pixelBlob = stbi_load(key.value.c_str(), &outReq.width, &outReq.height, &outReq.numChannels, STBI_rgb_alpha);
            outReq.size = outReq.width * outReq.height * 4;
        }

        if (!outReq.pixelBlob) {
            std::printf("%s error: could not load file '%s'\n", __FUNCTION__, key.value.c_str());
            stbi_image_free(outReq.pixelBlob);
            return false;
        }

        outReq.mipLevels += static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight))));
        

        return true;

	}

    // This feels sketchy
    void TextureGltfSource::destroyTexture(unsigned char* pixelBlob)
    {
        stbi_image_free(pixelBlob);
    }

}