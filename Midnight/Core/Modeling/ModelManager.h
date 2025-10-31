#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>

#include "Core/aveng_model.h"
#include "Core/Modeling/AssimpInstance.h"

namespace aveng {
	class ModelManager {



		std::vector<std::shared_ptr<AvengModel>> miModelList{};
		int miSelectedModel = 0;

		std::vector<std::shared_ptr<AssimpInstance>> miAssimpInstances{};
		std::unordered_map<std::string, std::vector<std::shared_ptr<AssimpInstance>>> miAssimpInstancesPerModel{};
		int miSelectedInstance = 0;
	};
}