#pragma once
#include "Module/Procgen/Types.h"
namespace procgen {

	static aveng::ErosionSettings DefaultHydraulicErosion() {

		aveng::HydraulicErosionParams hydraulic {
			0.05,  // pInertia;
			4.0,   // pCapacity;
			0.0005,// pDeposition;
			0.0005,// pErosion;
			0.02,  // pEvaporation;
			0.001, // pMinSlope;
			0.05,  // gravity;
			2000,  // numDroplets;
			30,    // numSteps;
		};

		aveng::ThermalErosionParams thermal {
			0.57,  // ~30 degrees angle of repose
			0.25,  // Transfer 25% of excess per iteration
			20,	   // 20 Iterations
		};

		aveng::HardnessParams hardness{
			0.7,
			0.3,
			0.01,
			0.1,
			2.0,
		};

		aveng::RidgeParams ridges {
			0.3,	// Moderate threshold
			1.5,	// Noticeable but not extreme
			0.3,	// Some jaggedness
			0.05,	// Medium-scale noise
			1,		// Iterations
			0.5,			// Only enhance upper half of terrain
			"normalized",	// Use normalized height (0-1 within chunk)
		};

		return aveng::ErosionSettings{
			hydraulic, thermal, hardness, ridges
		};

	}

}