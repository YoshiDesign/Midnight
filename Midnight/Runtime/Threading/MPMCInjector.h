#pragma once
namespace mtools {

	template <typename T>
	struct MPMCInjector
	{
		// For now this is just a placeholder for the future multi-producer multi-consumer job injector.
		// The idea is to have a single global injector that all systems can submit jobs to, and all workers can steal from.
		// This will replace the current single-producer single-consumer injectors that are currently embedded within each system (e.g. TerrainController).
	};
}