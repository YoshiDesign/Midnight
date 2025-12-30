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
        std::vector<AnyInstanceHandle> selectedMany; // For future reference
        int selectedEditorInstance = 0;
        // AssimpInstance* eCurrentSelectedInstance = nullptr;
        AnyInstanceHandle selectedInstance{};
        bool highlight = false;
        float blink = 0.1f;

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

        bool eHighlightSelectedInstance = false;
        bool eHasSelection = false;
        float eSelectHighlightValue = 1.0f;

        int eManyInstanceCreateNum = 1;
        int eManyInstanceCloneNum = 1;


        //int miSelectedEditorInstance = 0;


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
