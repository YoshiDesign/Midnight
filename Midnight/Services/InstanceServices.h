#pragma once
#include "Core/Modeling/ModelAndInstanceData.h"

namespace aveng {
	/* Currently implemented only by the SceneFacade */
	struct IInstanceQuery {

		/*
			IInstanceQuery is great for editor tools (select this handle, center camera, list all instances), 
			but it's the wrong shape for building draw batches:

			It forces per-handle lookups (N calls, branchy, cache-ugly).
			It hides the SoA-ish containers we've already built (instancesPerModel, dirtyGpuList, etc.).
			It encourages reconstructing what we already have.
		*/

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