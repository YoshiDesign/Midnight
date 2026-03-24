#pragma once
#include <string>
#include <cstdint>
#include "Runtime/Play/GameContext.h"
#include "Core/Input/InputState.h"
#include "Core/Camera/aveng_camera.h"

namespace xone {

    struct TickContext {
        float dt{};
        uint64_t frameIndex;
        const aveng::InputState& input;
        const aveng::AvengCamera& camera;
    };

    struct RenderContext {
        // Later: command buffer, frame index, render graph, etc.
    };

    class IGameInstance {
    public:
        virtual ~IGameInstance() = default;

        // Lifecycle
        virtual void onEnter() {}
        virtual void onExit() {}

        // Per-frame - state mutation
        virtual void update(const TickContext& ctx) = 0;

        /**
        * render()
            UI/HUD/menu drawing (especially if it’s game-specific, not editor UI)
            game-specific debug (hitboxes, paths, AI cones)
            special-case rendering hooks (e.g., draw a vignette, screen flash, cutscene letterbox)
            eventually building a render packet / render graph inputs         * 
        */
        virtual void render(const RenderContext& ctx) = 0;

        // Optional: for debugging/UI
        virtual std::string name() const = 0;
    };

}
