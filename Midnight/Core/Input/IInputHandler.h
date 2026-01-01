#pragma once
#include "Editor/Editor.h"
// #include "Core/Input/EventPayloads.h"
#include "Core/Input/InputState.h"
#include "Game/data.h"

namespace aveng {

    // Purely virtual base
    struct IInputHandler {
        virtual ~IInputHandler() = default; // Virtual to avoid skipping derived destructors
        virtual void onMouseButton(const MouseButtonEvent&) = 0;
        virtual void onKey(const KeyEvent&) = 0;
        virtual void onMouseMove(const MouseMoveEvent&) = 0;

        // Editor Only at the moment
        virtual void setMode(const AppMode& mode) { /* Base class, no-op without editor enabled */ };
    };

    struct GameInput : IInputHandler {
        explicit GameInput() {};
        ~GameInput() {}

        void onMouseButton(const MouseButtonEvent& e) override {/*TODO*/ }
        void onKey(const KeyEvent& e) override { /*TODO*/ }
        void onMouseMove(const MouseMoveEvent& e) { /*TODO*/ }

        void update(const InputState& inputState, float dt) { /*TODO*/ };

    };

#ifdef ENABLE_EDITOR
    struct EditorInput : IInputHandler {
        explicit EditorInput(Editor* _editor) : editor(_editor) {}
        ~EditorInput() {}
        Editor* editor;

        /* */
        void onMouseButton(const MouseButtonEvent& e) override {
            if (editor == nullptr) {
                throw std::runtime_error("Editor is nullptr! 2");
            }

            editor->handleMouseClick(e);
        }

        /* */
        void onKey(const KeyEvent& e) override {
            switch (e.key)
            {
            case GLFW_KEY_SPACE: if (e.action == GLFW_PRESS) break;
            case GLFW_KEY_PERIOD: if (e.action == GLFW_PRESS) editor->startGame();
                break;

            default:
                break;
            }

        }

        /* */
        void onMouseMove(const MouseMoveEvent& e) {
            if (editor == nullptr) {
                throw std::runtime_error("Editor is nullptr! 1");
            }

            editor->handleMouseMove(e);
        }

    };

    struct EditorGameRouter : IInputHandler {
        EditorGameRouter(AppMode& _mode, IInputHandler& _editor, IInputHandler& _game)
            : mode(_mode), editor(_editor), game(_game) {
        }

        ~EditorGameRouter() {}

        AppMode& mode;
        IInputHandler& editor;
        IInputHandler& game;

        void onMouseButton(const MouseButtonEvent& e) override {
            (mode == AppMode::Editor ? editor : game).onMouseButton(e);
        }
        void onKey(const KeyEvent& e) override {
            (mode == AppMode::Editor ? editor : game).onKey(e);
        }
        void onMouseMove(const MouseMoveEvent& e) {
            (mode == AppMode::Editor ? editor : game).onMouseMove(e);
        }

        // Editor Only at the moment
        void setMode(const AppMode& _mode) {
            mode = _mode;
        }
    };
#endif

}
