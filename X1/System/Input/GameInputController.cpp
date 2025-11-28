#include "GameInputController.h"

namespace aveng {

    void GameInputController::onMouseMove(const MouseMoveEvent& e) 
    {
        mGame.handleMouseMove(e);
    }

    void GameInputController::onMouseButton(const MouseButtonEvent& e) 
    {
        mGame.handleMouseClick(e);
    }

    // Non game related callbacks. See InputController for game button events
    void GameInputController::onKey(const KeyEvent& e)
    {
        mGame.startEditor();
    }
}