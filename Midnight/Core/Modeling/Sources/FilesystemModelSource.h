#pragma once
#include "avpch.h"
#include "IModelSource.h"
#include <vector>
#include <cstddef>

namespace aveng {

	class FilesystemModelSource : public IModelSource {
	public:
		std::vector<std::byte> readModelBytes(const AssetKey& key) override;
	};

}