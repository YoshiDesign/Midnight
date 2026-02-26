#include "ErosionManager.h"
#include "Module/Procgen/Terrain/Erosion/Presets.h"

namespace procgen {

	// Pass threads in case we haven't init'd yet.
	bool ErosionManager::switchToDefaultSettings(uint16_t nThreads)
	{
		if (settings_.size() == 0 && nThreads != 0) {
			// Init
			settings_.reserve(12); // arbitrary capacity
			settings_.push_back(DefaultHydraulicErosion(nThreads));
			activeSettings_ = settings_[0];
			return true;
		}
		else {
			activeSettings_ = settings_[0];
			return true;
		}

		return false;
	}

}