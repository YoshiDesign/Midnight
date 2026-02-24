#pragma once
#include <vector>
#include "Module/Procgen/Types.h"

namespace procgen {

	/*
	* Erosion Notes:
	* Don't parallelize any one erosion stage if the number of points is < a threshold. (e.g. 16k)
	*	Keep in mind that chunks are produced in parallel already, so subtasks can cause quite the storm.
	*   Each erosion stage can easily calculate a heuristic for itself to determine whether to parallelize or not:
	*	- based on the amount of work to be done (e.g. number of droplets, number of points in a batch)
	*   - based on the number of threads currently active, or the amount of work in the queue
	*	In other words, if coarse parallelism isn't saturating the cpu, then turn on some fine parallelism.
	* 
	* We should be able to interleave Hydraulic and Thermal erosion into epochs of processing. Each pass writes to a delta
	* buffer, which gets accumulated over each epoch before being applied to `workHeights`
	* These epochs can run on every point, or a subset; that's more of a visual design choice.
	*/

	struct ErosionManager {
		ErosionManager() = default;
		~ErosionManager() = default;

		aveng::ErosionSettings getActiveSettings() const { return activeSettings; }
		aveng::ErosionSettings setActiveSettings(size_t setIdx) { activeSettings = settings[setIdx]; return activeSettings; }

		void setHydraulicErosionParams(aveng::HydraulicErosionParams p) {} // TODO
		void setThermalErosionParams(aveng::ThermalErosionParams p) {} // TODO
		void setHardnessParams(aveng::HardnessParams p) {} // TODO
		void setRidgeParams(aveng::RidgeParams p) {} // TODO

		aveng::ErosionSettings addCustomSettings( // TODO
			aveng::HydraulicErosionParams hp,
			aveng::ThermalErosionParams tp,
			aveng::HardnessParams hrp,
			aveng::RidgeParams rp);

	private:
		// Selectable settings, whether presets or user-defined
		std::vector<aveng::ErosionSettings> settings;
		aveng::ErosionSettings activeSettings;

	};
}