#pragma once
#include "CoreVK/VkRenderData.h"
#include "Core/Modeling/AssimpInstance.h"
#include "Game/Camera/CameraManager.h"
#include "Core/Modeling/ModelAndInstanceData.h"
#include "avpch.h"

namespace aveng {

    // Editor sink to emit commands to XOne
    struct EditorCommand {
        enum class Type { RequestPlay, RequestStop, RequestPause, RequestResume } type;
        std::string payload; // e.g. "holyship"
    };

    struct EditorData {

        // --- model selection ---
        ModelId selectedModelId = 0;          // authoritative
        AssetKey selectedModelKey{};
        int selectedModelIndex = 0;           // For UI lists, to enable highlighted rows and whatnot.

        // --- instance selection ---
        AnyInstanceHandle primarySelection{}; // last clicked / active
        std::vector<AnyInstanceHandle> selectedMany; // multi-select set (unique)
        AnyInstanceHandle noSelect{};

        bool eHasSelection = false;
        ModelRef curModelSelect{};

        bool eHighlightSelectedInstance = false;
        float eSelectHighlightValue = 1.0f;

        int eManyInstanceCreateNum = 1;
        int eManyInstanceCloneNum = 1;

        std::vector<EditorCommand> commands;

        void requestPlay(std::string_view game) {
            commands.push_back({ EditorCommand::Type::RequestPlay, std::string(game) });
        }
        void requestStop() {
            commands.push_back({ EditorCommand::Type::RequestStop, {} });
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

        bool eMousePick = false;
        bool eMouseLock = false;
        int eMouseXPos = 0;
        int eMouseYPos = 0;
        int eMouseLastClickX = 0;
        int eMouseLastClickY = 0;
        bool eMouseMove = false;
        bool eMouseMoveVertical = false;
        int eMouseMoveVerticalShiftKey = 0;
    };

}
