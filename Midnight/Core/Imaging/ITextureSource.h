#pragma once

#include "CoreVK/Resources/MTexture.h"

namespace aveng {

    struct ITextureSource {
        virtual ~ITextureSource() = default;
        virtual bool loadTexture(const TextureAssetKey& key, TextureCreateRequest& outReq) = 0;
    };

}