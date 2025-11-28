#pragma once
#include "CoreVK/VkRenderData.h"
#include "Core/Modeling/AssimpInstance.h"

namespace aveng {

    struct EditorData {
        /* color hightlight for selection etc */
        std::vector<glm::vec2> eSelectedInstance{}; // Shader Uniform Data

        bool eHighlightSelectedInstance = false;
        bool eHasSelection = false;
        float eSelectHighlightValue = 1.0f;

        int eManyInstanceCreateNum = 1;
        int eManyInstanceCloneNum = 1;

        std::shared_ptr<AssimpInstance> eCurrentSelectedInstance = nullptr;

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
