#pragma once
#include "Core/Input/EventPayloads.h"
#include <array>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

namespace aveng {

    struct InputState {

        static constexpr int MaxKeys = GLFW_KEY_LAST + 1;
        static constexpr int MaxMouseButtons = GLFW_MOUSE_BUTTON_LAST + 1;

        // --- held ---
        std::array<bool, MaxKeys> keyDown{};
        std::array<bool, MaxMouseButtons> mouseDown{};

        // --- edge (reset per frame) ---
        std::array<bool, MaxKeys> keyPressed{};
        std::array<bool, MaxKeys> keyReleased{};
        std::array<bool, MaxMouseButtons> mousePressed{};
        std::array<bool, MaxMouseButtons> mouseReleased{};

        // --- mouse ---
        double mouseX = 0.0;
        double mouseY = 0.0;
        double prevMouseX = 0.0;
        double prevMouseY = 0.0;
        double mouseDeltaX = 0.0;
        double mouseDeltaY = 0.0;
        double prevLClickX = 0.0; // Unused
        double prevRClickY = 0.0; // Unused

        // --- scroll (per frame) ---
        double scrollX = 0.0;
        double scrollY = 0.0;

        // --- modifiers ---
        bool shift = false;
        bool ctrl = false;
        bool alt = false;
        bool super = false;

        // --- text input (unused at the moment) ---
        // std::vector<uint32_t> text; // UTF-32 codepoints this frame

        void beginFrame() {
            // compute deltas at frame boundary (or do it in endFrame)
            mouseDeltaX = mouseX - prevMouseX;
            mouseDeltaY = mouseY - prevMouseY;
            prevMouseX = mouseX;
            prevMouseY = mouseY;

            // clear per-frame accumulators + edges
            scrollX = scrollY = 0.0;
            // text.clear();

            keyPressed.fill(false);
            keyReleased.fill(false);
            mousePressed.fill(false);
            mouseReleased.fill(false);
        }
    };

}