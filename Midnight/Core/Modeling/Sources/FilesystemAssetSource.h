#pragma once
#include "avpch.h"
#include "IAssetSource.h"
#include <vector>
#include <cstddef>

namespace aveng {

	class FilesystemAssetSource : public IAssetSource {
	public:
		std::vector<std::byte> readModelBytes(const AssetKey& key) override;
	};

}