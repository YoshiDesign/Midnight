#pragma once

namespace aveng {

    //struct Button {
    //    bool down = false;
    //    bool pressed = false;
    //    bool released = false;
    //};

    struct MouseMoveEvent {
        double x;
        double y;
        bool rmbDown;
    };

    struct MouseButtonEvent {
        int button;
        int action;
        int mods;
        double x;
        double y;
    };

    struct KeyEvent {
        int key;
        int scancode;
        int action;
        int mods;
    };
}