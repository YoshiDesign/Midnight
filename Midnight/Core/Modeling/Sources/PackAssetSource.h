#pragma once
#include "avpch.h"
#include "IAssetSource.h"

namespace aveng {

	class PackAssetSource : public IAssetSource {
	public:
		std::vector<std::byte> readModelBytes(const AssetKey& key) override;
	};

}