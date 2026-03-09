#pragma once
#include "Core/Imaging/ITextureSource.h"
namespace aveng {

	struct TextureFilesystemSource : public ITextureSource {
		bool loadTexture(TextureAssetID assetId, TextureCreateRequest& outReq) override;
	};

}