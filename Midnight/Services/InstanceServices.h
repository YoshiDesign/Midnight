#pragma once
#include "Core/Modeling/ModelAndInstanceData.h"

namespace aveng {

	struct InstanceView {
		ModelId modelId;
		InstanceTransform xf;
		glm::mat4 modelRoot;
		bool animated;
	};

	struct IInstanceQuery {
		virtual ~IInstanceQuery() = default;

		// Single-instance read
		virtual bool tryGetInstance(AnyInstanceHandle h, InstanceView& out) const = 0;

		// Listing for outliner
		virtual std::vector<AnyInstanceHandle> listAllInstances() const = 0;
		virtual std::vector<AnyInstanceHandle> listInstancesForModel(ModelId id) const = 0;

		// Optional: selection validation helper
		virtual bool isAlive(AnyInstanceHandle h) const = 0;
	};

}