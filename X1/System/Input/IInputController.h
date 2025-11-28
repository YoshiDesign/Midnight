#pragma once
#include "EventPayloads.h"

namespace aveng {

    struct IInputController {
        virtual ~IInputController() = default;
        virtual void onMouseMove(const MouseMoveEvent& e) = 0;
        virtual void onMouseButton(const MouseButtonEvent& e) = 0;
        virtual void onKey(const KeyEvent& e) = 0;
    };

}