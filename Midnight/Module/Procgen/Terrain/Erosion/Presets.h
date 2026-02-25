#pragma once
#include "Module/Procgen/Types.h"
namespace procgen {

	static aveng::ErosionSettings DefaultHydraulicErosion() {

		aveng::HydraulicErosionParams hydraulic {
			60000,	// total droplets
			32,		// steps per droplet (upper bound)
			2048,	// droplets per task
			8,		// threads max

			0.05f,	// Pinertia
			4.0f,
			4.0f,
			0.01f,
			0.3f,
			0.3f,
			0.01f,

			16.0f,
			1e-4f,   // threshold for "close to 0" slope
			1e-4f,   // threshold for "nearly 0" capacity
			0.35f,   // additional evaporation factor when flat+low-capdrostate
			0.8f,
			1.0f
		};

		aveng::ThermalErosionParams thermal {
			0.57,  // ~30 degrees angle of repose
			0.25,  // Transfer 25% of excess per iteration
			20	   // 20 Iterations
		};

		aveng::HardnessParams hardness{
			0.7,
			0.3,
			0.01,
			0.1,
			2.0,
			true // enabled
		};

		aveng::RidgeParams ridges {
			0.3,	// Moderate threshold
			1.5,	// Noticeable but not extreme
			0.3,	// Some jaggedness
			0.05,	// Medium-scale noise
			1,		// Iterations
			0.5,			// Only enhance upper half of terrain
			"normalized"	// Use normalized height (0-1 within chunk)
		};

		return aveng::ErosionSettings{
			hydraulic, thermal, hardness, ridges
		};

	}

}