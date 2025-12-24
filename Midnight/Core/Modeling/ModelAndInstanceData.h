/* separate settings file to avoid cicrula dependecies */
#pragma once

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <string>

namespace aveng {
	// forward declaration
	class AvengModel;
	class AssimpInstance;

	using modelCheckCallback = std::function<bool(std::string)>;
	using modelAddCallback = std::function<bool(std::string)>;
	using modelDeleteCallback = std::function<void(std::string)>;

	using instanceAddCallback = std::function<std::shared_ptr<AssimpInstance>(std::shared_ptr<AvengModel>)>;
	using instanceAddManyCallback = std::function<void(std::shared_ptr<AvengModel>, int)>;
	using instanceDeleteCallback = std::function<void(std::shared_ptr<AssimpInstance>)>;
	using instanceCloneCallback = std::function<void(std::shared_ptr<AssimpInstance>)>;
	using instanceCloneManyCallback = std::function<void(std::shared_ptr<AssimpInstance>, int)>;

	using instanceCenterCallbackEditor = std::function<void(std::shared_ptr<AssimpInstance>)>;

	struct ModelAndInstanceData {

		// A list of the unique models currently loaded
		std::vector<std::shared_ptr<AvengModel>> miModelList{};
		int miSelectedEditorModel = 0;

		// A list of all instances being rendered
		std::vector<std::shared_ptr<AssimpInstance>> miAssimpInstances{};

		// The same list, in { "modelFilename" : [instances] } format
		std::unordered_map<std::string, std::vector<std::shared_ptr<AssimpInstance>>> miAssimpInstancesPerModel{};

		int miSelectedEditorInstance = 0;

		/* pending list for deletion - outside of command buffer recording */
		std::unordered_set<std::shared_ptr<AvengModel>> miPendingDeleteAvengModels{};

		// Filepaths pending loading
		std::vector<std::string> mPendingModelLoads;

		/* callbacks */
		modelCheckCallback miModelCheckCallbackFunction;
		modelAddCallback miModelAddCallbackFunction;
		modelDeleteCallback miModelDeleteCallbackFunction;

		instanceAddCallback miInstanceAddCallbackFunction;
		instanceAddManyCallback miInstanceAddManyCallbackFunction;
		instanceDeleteCallback miInstanceDeleteCallbackFunction;
		instanceCloneCallback miInstanceCloneCallbackFunction;
		instanceCloneManyCallback miInstanceCloneManyCallbackFunction;

		instanceCenterCallbackEditor miInstanceCenterCallbackFunction;
	};

}