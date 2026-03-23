#pragma once
// #include "CoreVK/VkRenderData.h"
#include "avpch.h"
#include "Core/Modeling/AssimpInstance.h"
#include "Game/Camera/CameraManager.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "Module/Procgen/Types.h"
#include "Module/Procgen/Noise/Config.h"

namespace aveng {

    // Editor sink to emit commands to XOne
    struct EditorCommand {
        enum class Type { 
            RequestPlay, 
            RequestStop, 
            RequestPause, 
            RequestResume, 
            GenerateTerrain,
            UpdateTerrainGlobalParams,
            UpdateTerrainNoiseParams,
            UpdateWeatheringParams
        } type;
        std::string payload; // e.g. "holyship"
        int terrain_x; // GenerateTerrain
        int terrain_z; // GenerateTerrain
        TerrainConfig tcfg; // UpdateTerrainGlobalParams
        noise::NoiseParams ncfg; // UpdateTerrainNoiseParams
        ErosionSettings erosion; // UpdateWeatheringParams
    };

    struct EditorData {

        // --- model selection ---
        ModelId selectedModelId = 0;          // authoritative
        AssetKey selectedModelKey{};
        int selectedModelIndex = 0;           // For UI lists, to enable highlighted rows and whatnot.
        bool implicitSelection = false;

        // --- instance selection ---
        AnyInstanceHandle primarySelection{}; // last clicked / active
        // int primarySelection; // last clicked / active
        std::vector<AnyInstanceHandle> selectedMany; // multi-select set (unique)
        AnyInstanceHandle noSelect{};

        ModelRef curModelSelect{};

        bool eHighlightSelectedInstance = false;
        float eSelectHighlightValue = 1.0f;

        int eManyInstanceCreateNum = 1;
        int eManyInstanceCloneNum = 1;

        std::vector<EditorCommand> commands;

        void requestPlay(std::string_view game) {
            EditorCommand ec;
            ec.type = EditorCommand::Type::RequestPlay;
            ec.payload = std::string(game);
            commands.push_back(ec);
        }
        void requestStop() {
            EditorCommand ec;
            ec.type = EditorCommand::Type::RequestStop;
            commands.push_back(ec);
        }
        void requestTerrain(int x, int z) {
            EditorCommand ec;
            ec.type = EditorCommand::Type::GenerateTerrain;
            ec.terrain_x = x;
            ec.terrain_z = z;
            commands.push_back(ec);
        }
        void requestTerrainGlobalParams(TerrainConfig tcfg) {
            EditorCommand ec;
            ec.type = EditorCommand::Type::UpdateTerrainGlobalParams;
            ec.tcfg = tcfg;
            commands.push_back(ec);
        }
        void requestTerrainNoiseParams(noise::NoiseParams noise) {
            EditorCommand ec;
            ec.type = EditorCommand::Type::UpdateTerrainNoiseParams;
            ec.ncfg = noise;
            commands.push_back(ec);
        }
        void requestTerrainWeatheringParams(ErosionSettings ero) {
            EditorCommand ec;
            ec.type = EditorCommand::Type::UpdateWeatheringParams;
            ec.erosion = ero;
            commands.push_back(ec);
        }

        std::vector<EditorCommand> drainCommands() {
            auto out = std::move(commands);
            commands.clear();
            return out;
        }

        /* color hightlight for selection etc */
        std::vector<glm::vec2> eSelectedInstance{}; // Shader Uniform Data

        CameraTransform cameraTransform{};
        std::vector<CameraDebugInfo> cameraDebugList;

        bool playHolyShip = false;

        bool eMousePick = false;
        bool eMouseLock = false;
        bool eShowTRSPanel = false;
        int eMouseXPos = 0;
        int eMouseYPos = 0;
        int eMouseLastClickX = 0;
        int eMouseLastClickY = 0;
        bool eMouseMove = false;
        bool eMouseMoveVertical = false;
        int eMouseMoveVerticalShiftKey = 0;
    };

}
