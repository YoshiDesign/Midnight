#pragma once

#include "Runtime/Play/IGameInstance.h"
#include "Runtime/Play/PlayManager.h"
#include <iostream>

namespace xone {

    class Starfield final : public IGameInstance {
    public:
        explicit Starfield(PlayManager& play) : play_(play) {}

        std::string name() const override { return "Starfield"; }

        void onEnter() override {
            std::cout << "[Starfield] Enter\n";
        }

        void onExit() override {
            std::cout << "[Starfield] Exit\n";
        }

        void update(const TickContext& ctx) override {
            // Pretend the user pressed "Start"
            elapsed_ += ctx.dt;
            if (elapsed_ > 1.0f && !started_) {
                started_ = true;
                play_.requestPlay("starfield");
            }
        }

        void render(const RenderContext&) override {
            // draw menu
        }

    private:
        PlayManager& play_;
        float elapsed_{};
        bool started_{ false };
    };

} // namespace xone
