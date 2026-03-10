#pragma once
#include "Core/Imaging/ITextureSource.h"
namespace aveng {

	struct TextureFilesystemSource : public ITextureSource {
		bool loadTexture(const TextureAssetKey& assetId, TextureCreateRequest& outReq) override;
		void destroyTexture(unsigned char* pixelBlob) override;
	};

}