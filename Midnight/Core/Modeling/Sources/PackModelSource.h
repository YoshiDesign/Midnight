#pragma once
#include "avpch.h"
#include "IModelSource.h"

namespace aveng {

	class PackModelSource : public IModelSource {
	public:
		std::vector<std::byte> readModelBytes(const AssetKey& key) override;
	};

}