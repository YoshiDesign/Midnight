#pragma once
#include "Module/Procgen/Types.h"
namespace procgen {

	/* Consider passing in the TerrainConfig to each, instead of nThreads - Add nThreads to TerrainConfig */

	static aveng::ErosionSettings DefaultErosion(uint16_t nThreads) {

		//aveng::HydraulicErosionParams hydraulic {
		//	60000,	// numDroplets
		//	32,		// maxSteps
		//	2048,	// batchSize
		//	std::floor(nThreads / 6), // maxWorkers: 1/6 of the hardware's threads

		//	0.05f,	 // inertia
		//	4.0f,	 // gravity 
		//	4.0f,	 // pCapacity 
		//	0.01f,	 // pMinSlope 
		//	0.3f,	 // pDeposition 
		//	0.3f,	 // pErosion 
		//	0.01f,	 // pEvaporation 

		//	16.0f,	 // spawnMargin
		//	1e-4f,   // flatSlopeEps
		//	1e-4f,   // flatCapEps
		//	0.35f,   // flatExtraEvap

		//	0.8f,	 // initWater
		//	1.0f	 // initVel
		//};

		aveng::HydraulicErosionParams hydraulic{
			60000,	// numDroplets
			32,		// maxSteps
			2048,	// batchSize
			std::floor(nThreads / 6), // maxWorkers: 1/6 of the hardware's threads

			0.25f,	 // inertia
			4.0f,	 // gravity 
			4.0f,	 // pCapacity 
			0.01f,	 // pMinSlope 
			0.3f,	 // pDeposition 
			0.5f,	 // pErosion 
			0.01f,	 // pEvaporation 

			16.0f,	 // spawnMargin
			1e-4f,   // flatSlopeEps
			1e-4f,   // flatCapEps
			0.35f,   // flatExtraEvap

			0.8f,	 // initWater
			1.0f	 // initVel
		};

		aveng::ThermalErosionParams thermal{
			0.57,  // ~30 degrees angle of repose
			0.30,  // Transfer 30% of excess per iteration
			30,	   // 30 Iterations
			std::floor(nThreads / 3)
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
			0.2f,	
			3.0f,
			0.8f,
			0.1f,
			2,
			0.4f,
			"normalized",
			12,
			true
		};

		return aveng::ErosionSettings{
			hydraulic, thermal, hardness, ridges
		};

	}

}