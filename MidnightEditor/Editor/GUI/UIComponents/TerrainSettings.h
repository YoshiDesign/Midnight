#pragma once
#include "Editor/GUI/imgui.h"
#include "Editor/GUI/imgui_impl_glfw.h"
#include "Editor/GUI/imgui_impl_vulkan.h"
#include "Editor/GUI/imgui_internal.h"
#include "Editor/GUI/ImGuiFileDialog.h"
#include "Editor/EditorData.h"
#include "CoreVK/VkRenderData.h"
namespace aveng {

    inline void DisplayTerrainTimers(VkRenderData& renderData) {
        ImGui::Text("Total Buffer + Descriptor:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainBufferTimeInitMAX);

        ImGui::Text("getOrCreate T1:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainManagerTimer_1MAX);
        ImGui::Text("Current:\t%.6f", renderData.rdTerrainManagerTimer_1);

        ImGui::Text("getOrCreate T2 GET only:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainManagerTimer_2MAX);
        ImGui::Text("Current:\t%.6f", renderData.rdTerrainManagerTimer_2);

        ImGui::Separator();

        ImGui::Text("1. VBO / IBO Init + Copy:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainVboIboTimeMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdTerrainVboIboTime);

        ImGui::Text("2. Compute SSBO1 CPU Input:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainSsbo1TimeMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdTerrainSsbo1Time);

        ImGui::Text("3. Compute SSBO2 GPU Resident Init:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainSsbo2TimeMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdTerrainSsbo2Time);

        ImGui::Text("4. Descriptors Alloc+Write+Update:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainDescriptor1WriterTimeMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdTerrainDescriptor1WriterTime);

        ImGui::Text("5. VBO/IBO recordCopy:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainCopyBufferTimeMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdTerrainCopyBufferTime);
        
        ImGui::Text("6. Gfx Queue Submit:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdqueueSubmitTimerMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdqueueSubmitTimer);

        ImGui::Separator();

        ImGui::Text("Deferred Deletion:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainCleanupDeferredDeletesTimeMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdTerrainCleanupDeferredDeletesTime);

        ImGui::Text("Eviction:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainEvictionTimeMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdTerrainEvictionTime);
   
        ImGui::Text("1-Chunk Retire:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainRetireTimeMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdTerrainRetireTime);
   
        ImGui::Text("1-Chunk Drain:\t");
        ImGui::SameLine();
        ImGui::Text("%.6f", renderData.rdTerrainDrainTimeMAX);
        ImGui::Text("Current: \t%.6f", renderData.rdTerrainDrainTime);
   
    }

	inline void DisplayTerrainSettings(EditorData& editorData, bool* show_terrain_timers) {
        if (ImGui::BeginTabItem("Terrain")) {
            static int terrainChunkX = 0;
            static int terrainChunkZ = 0;

            ImGui::Checkbox("Terrain Timers", show_terrain_timers);

            ImGui::Text("Generate Terrain");

            ImGui::TextUnformatted("X");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputInt("##TerrainChunkX", &terrainChunkX);

            ImGui::TextUnformatted("Z");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            ImGui::InputInt("##TerrainChunkZ", &terrainChunkZ);

            if (ImGui::Button("Generate", { 350, 50 })) {
                editorData.requestTerrain(terrainChunkX, terrainChunkZ);
            }

            if (ImGui::CollapsingHeader("Global Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                static int worldSeed = 42;
                ImGui::TextUnformatted("World Seed");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputInt("##TerrainWorldSeed", &worldSeed);

                static float chunkSize = 256.0f;
                ImGui::TextUnformatted("Chunk Size");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputFloat("##TerrainChunkSize", &chunkSize, 0.0f, 0.0f, "%.6f");

                static float minPointDist = 8.0f;
                ImGui::TextUnformatted("Minimum Point Distance");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputFloat("##TerrainMinPointDist", &minPointDist, 0.0f, 0.0f, "%.6f");

                static float haloDist = 32.0f;
                ImGui::TextUnformatted("Halo Width");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputFloat("##TerrainHaloWidth", &haloDist, 0.0f, 0.0f, "%.6f");

                if (ImGui::Button("Save Globals", { 350, 50 })) {
                    TerrainConfig newCfg;
                    newCfg.worldSeed = worldSeed;
                    newCfg.chunkSize = chunkSize;
                    newCfg.minPointDist = minPointDist;
                    newCfg.halo = haloDist;

                    editorData.requestTerrainGlobalParams(newCfg);
                }
            }

            if (ImGui::CollapsingHeader("Height-Field Noise Params", ImGuiTreeNodeFlags_DefaultOpen)) {
                static int nOctaves = 7;
                ImGui::TextUnformatted("Octaves");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputInt("##TerrainHFOctaves", &nOctaves);

                static float nFrequency = 0.01f;
                ImGui::TextUnformatted("Frequency");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputFloat("##TerrainHFFrequency", &nFrequency, 0.0f, 0.0f, "%.6f");

                static float nAmplitude = 32.f;
                ImGui::TextUnformatted("Amplitude");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputFloat("##TerrainHFAmplitude", &nAmplitude, 0.0f, 0.0f, "%.6f");

                static float nPersistence = 0.5f;
                ImGui::TextUnformatted("Persistence");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputFloat("##TerrainHFPersistence", &nPersistence, 0.0f, 0.0f, "%.6f");

                static float nLacunarity = 2.0f;
                ImGui::TextUnformatted("Lacunarity");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::InputFloat("##TerrainHFLacunarity", &nLacunarity, 0.0f, 0.0f, "%.6f");

                if (ImGui::Button("Save Noise Params", { 350, 50 })) {
                    noise::NoiseParams ncfg;
                    ncfg.octaves = nOctaves;
                    ncfg.frequency = nFrequency;
                    ncfg.amplitude = nAmplitude;
                    ncfg.persistence = nPersistence;
                    ncfg.lacunarity = nLacunarity;

                    std::printf("Saving Noise Params\n");
                    editorData.requestTerrainNoiseParams(ncfg);
                }
            }

            if (ImGui::CollapsingHeader("Weathering Stages", ImGuiTreeNodeFlags_DefaultOpen)) {

                static ErosionSettings weathering;

                static int hMaxWorkers = std::floor(std::thread::hardware_concurrency() / 6);
                static int tMaxWorkers = std::floor(std::thread::hardware_concurrency() / 9);
                static int rMaxWorkers = 1;

                // Hardness
                static float elevationWeight = 0.7f;
                static float noiseWeight = 0.3f;
                static float noiseFrequency = 0.01f;
                static float baseHardness = 0.1f;
                static float elevationPower = 2.0f;

                // Hydro
                static int hNumDroplets = 10000;
                static int hMaxSteps = 32;
                static int hBatchSize = 2048;
                static float hInertia = 0.25f;
                static float hGravity = 4.0f;
                static float hCapacity = 4.0f;
                static float hMinSlope = 0.01f;
                static float hDeposition = 0.3f;
                static float hErosion = 0.5f;
                static float hEvaporation = 0.01f;
                static float hSpawnMargin = 16.0f;
                static float hFlatSlopeEps = 1e-4f;
                static float hFlatCapEps = 1e-4f;
                static float hFlatExtraEvap = 0.35f;
                static float hInitWater = 0.8f;
                static float hInitVel = 1.0f;

                // Thermal
                static float tTalusThreshold = 0.57f;
                static float tTransferRate = 0.30f;
                static int tIterations = 30;

                // Ridge
                static float rThreshold = 0.2f;
                static float rBoostAmount = 3.0f;
                static float rNoiseAmount = 0.8f;
                static float rNoiseFreq = 0.1f;
                static int rIterations = 2;
                static float rMinHeight = 0.4f;
                static int rMinHeightMode = 1;


                static bool hardness = true;
                ImGui::TextUnformatted("Compute Hardness");
                ImGui::SameLine(200.f);
                ImGui::SetNextItemWidth(200.0f);
                ImGui::Checkbox("##TerrainHardness", &hardness);

                if (hardness) {
                    ImGui::Indent();

                    ImGui::TextUnformatted("Elevation Weight");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HardnessElevWeight", &elevationWeight, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Noise Weight");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HardnessNoiseWeight", &noiseWeight, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Noise Frequency");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HardnessNoiseFreq", &noiseFrequency, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Base Hardness");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HardnessBase", &baseHardness, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Elevation Power");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HardnessElevPower", &elevationPower, 0.0f, 0.0f, "%.6f");

                    ImGui::Unindent();

                }

                static bool hydro = true;
                ImGui::TextUnformatted("Hydraulic Erosion");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                ImGui::Checkbox("##TerrainHydroErosion", &hydro);

                if (hydro) {
                    ImGui::Indent();

                    ImGui::TextUnformatted("CPU Threads");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputInt("##HydroThreads", &hMaxWorkers);

                    ImGui::Text("Droplets____________________________");
                    // Droplet Params
                    ImGui::TextUnformatted("Num Droplets");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputInt("##HydroNumDroplets", &hNumDroplets);

                    ImGui::TextUnformatted("Initial Water");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroInitWater", &hInitWater, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Initial Velocity");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroInitVel", &hInitVel, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Sediment Capacity");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroCapacity", &hCapacity, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Max Steps");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputInt("##HydroMaxSteps", &hMaxSteps);

                    ImGui::TextUnformatted("Evaporation Rate");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroEvaporation", &hEvaporation, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Flatness Evaporation Amplifier");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroFlatExtraEvap", &hFlatExtraEvap, 0.0f, 0.0f, "%.6f");

                    ImGui::Text("Erosion_____________________________");
                    // Erosion Coeff's
                    ImGui::TextUnformatted("Erosion Rate");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroErosion", &hErosion, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Deposition Rate");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroDeposition", &hDeposition, 0.0f, 0.0f, "%.6f");

                    ImGui::Text("Physical Constants__________________");
                    // Physical Constants
                    ImGui::TextUnformatted("Inertia");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroInertia", &hInertia, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Gravity");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroGravity", &hGravity, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Flat Slope Epsilon");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroFlatSlopeEps", &hFlatSlopeEps, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Flat Capacity Epsilon");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroFlatCapEps", &hFlatCapEps, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Min Slope Threshold");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroMinSlope", &hMinSlope, 0.0f, 0.0f, "%.6f");

                    ImGui::Text("Misc.______________________________");
                    // Sim Limits
                    ImGui::TextUnformatted("Outer Spawn Margin");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##HydroSpawnMargin", &hSpawnMargin, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Droplets Per Thread");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputInt("##HydroBatchSize", &hBatchSize);

                    ImGui::Unindent();
                }

                static bool thermal = true;
                ImGui::TextUnformatted("Thermal Erosion");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                ImGui::Checkbox("##TerrainThermalErosion", &thermal);

                if (thermal) {
                    ImGui::Indent();

                    ImGui::TextUnformatted("CPU Threads");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputInt("##ThermalThreads", &tMaxWorkers);

                    ImGui::TextUnformatted("Talus Threshold");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##ThermalTalus", &tTalusThreshold, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Transfer Rate");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##ThermalTransfer", &tTransferRate, 0.0f, 0.0f, "%.6f");

                    ImGui::TextUnformatted("Iterations");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputInt("##ThermalIterations", &tIterations);



                    ImGui::Unindent();
                }

                static bool ridgeEnhance = true;
                ImGui::TextUnformatted("Ridge Enhancement");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(200.0f);
                ImGui::Checkbox("##TerrainRidgeEnhance", &ridgeEnhance);

                if (ridgeEnhance) {
                    ImGui::Indent();

                    ImGui::TextUnformatted("CPU Threads");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputInt("##RidgeThreads", &rMaxWorkers);

                    ImGui::TextUnformatted("Threshold");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##RidgeThreshold", &rThreshold, 0.0f, 0.0f, "%.6f");


                    ImGui::TextUnformatted("Boost Amount");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##RidgeBoost", &rBoostAmount, 0.0f, 0.0f, "%.6f");


                    ImGui::TextUnformatted("Noise Amount");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##RidgeNoiseAmt", &rNoiseAmount, 0.0f, 0.0f, "%.6f");


                    ImGui::TextUnformatted("Noise Frequency");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##RidgeNoiseFreq", &rNoiseFreq, 0.0f, 0.0f, "%.6f");


                    ImGui::TextUnformatted("Iterations");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputInt("##RidgeIterations", &rIterations);


                    ImGui::TextUnformatted("Min Height");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::InputFloat("##RidgeMinHeight", &rMinHeight, 0.0f, 0.0f, "%.6f");


                    const char* heightModes[] = { "Absolute", "Normalized" };
                    ImGui::TextUnformatted("Min Height Mode");
                    ImGui::SameLine(200.f);
                    ImGui::SetNextItemWidth(200.0f);
                    ImGui::Combo("##RidgeMinHeightMode", &rMinHeightMode, heightModes, IM_ARRAYSIZE(heightModes));

                    ImGui::Unindent();
                }

                if (ImGui::Button("Save Weathering Params", { 350, 50 })) {

                    // Stage Enablers
                    weathering.hardnessMapEnabled = hardness;
                    weathering.ridgeEnhancementEnabled = ridgeEnhance;
                    weathering.thermalErosionEnabled = thermal;
                    weathering.hydraulicErosionEnabled = hydro;

                    HardnessParams pHardness;
                    pHardness.ElevationWeight = elevationWeight;
                    pHardness.NoiseWeight = noiseWeight;
                    pHardness.NoiseFrequency = noiseFrequency;
                    pHardness.BaseHardness = baseHardness;
                    pHardness.ElevationPower = elevationPower;

                    // Set hardness
                    weathering.hardness = pHardness;

                    ThermalErosionParams pTherm;
                    pTherm.maxWorkers = tMaxWorkers;
                    pTherm.TalusThreshold = tIterations;
                    pTherm.TransferRate = tTransferRate;
                    pTherm.Iterations = tTalusThreshold;

                    // Set thermal
                    weathering.thermal = pTherm;

                    HydraulicErosionParams pHydro;
                    pHydro.maxWorkers = hMaxWorkers;
                    pHydro.numDroplets = hNumDroplets;
                    pHydro.maxSteps = hMaxSteps;
                    pHydro.batchSize = hBatchSize;
                    pHydro.inertia = hInertia;
                    pHydro.gravity = hGravity;
                    pHydro.pCapacity = hCapacity;
                    pHydro.pMinSlope = hMinSlope;
                    pHydro.pDeposition = hDeposition;
                    pHydro.pErosion = hErosion;
                    pHydro.pEvaporation = hEvaporation;
                    pHydro.spawnMargin = hSpawnMargin;
                    pHydro.flatSlopeEps = hFlatSlopeEps;
                    pHydro.flatCapEps = hFlatCapEps;
                    pHydro.flatExtraEvap = hFlatExtraEvap;
                    pHydro.initWater = hInitWater;
                    pHydro.initVel = hInitVel;

                    // Set hydraulic
                    weathering.hydraulic = pHydro;

                    RidgeParams pRidge;
                    pRidge.MaxWorkers = rMaxWorkers;
                    pRidge.Threshold = rThreshold;
                    pRidge.BoostAmount = rBoostAmount;
                    pRidge.NoiseAmount = rNoiseAmount;
                    pRidge.NoiseFreq = rNoiseFreq;
                    pRidge.Iterations = rMinHeightMode;
                    pRidge.MinHeight = rMinHeightMode;
                    pRidge.MinHeightMode = rMinHeightMode;

                    // Set ridges
                    weathering.ridges = pRidge;

                    // Send it
                    editorData.requestTerrainWeatheringParams(weathering);
                }

            }

            ImGui::EndTabItem();
        }
	
	}

}