#include "EditorInputController.h"
#include <stdexcept>
/*
* Interpret raw input into editor actions
*/
namespace aveng {

    void EditorInputController::onMouseMove(const MouseMoveEvent& e) 
    {
        if (mEditor == nullptr) {
            throw std::runtime_error("Editor is nullptr! 1");
        }
        // Example
        // if (e.rmbDown && editor.viewportHasFocus()) {
            // editor.orbitCamera(dx, dy);
        // }
        //std::cout << "Mouse Moved in Editor!" << std::endl;
        mEditor->handleMouseMove(e);
    }

    void EditorInputController::onMouseButton(const MouseButtonEvent& e) 
    {
        if (mEditor == nullptr) {
            throw std::runtime_error("Editor is nullptr! 2");
        }
        std::cout << "Mouse Clicked in Editor!" << std::endl;
        mEditor->handleMouseClick(e);
    }

    void EditorInputController::onKey(const KeyEvent& e)
    {
        switch (e.key)
        {
        case GLFW_KEY_SPACE: if (e.action == GLFW_PRESS) break;
        case GLFW_KEY_PERIOD: if (e.action == GLFW_PRESS) mEditor->startGame();
            break;

        default:
            break;
        }
        std::cout << "Editor keypress:\t" << e.key << std::endl;
    }

}