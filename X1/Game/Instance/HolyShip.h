#pragma once

#include "Runtime/Play/IGameInstance.h"
#include "Runtime/Play/PlayManager.h"
#include "avpch.h"

namespace xone {

    class HolyShip final : public IGameInstance {
    public:
        explicit HolyShip(PlayManager& play) : play_(play) {}

        std::string name() const override { return "HolyShip"; }

        void onEnter() override {
            std::cout << "[HolyShip] Enter\n";
        }

        void onExit() override {
            std::cout << "[HolyShip] Exit\n";
        }

        void update(const TickContext& ctx) override {
            // Pretend the user pressed "Start"
            elapsed_ += ctx.dt;
            if (elapsed_ > 1.0f && !started_) {
                started_ = true;
                play_.requestPlay("holyship");
            }
            std::cout << "[HolyShip] !! \n";
        }

        void render(const RenderContext&) override {
            // draw game
        }

    private:
        PlayManager& play_;
        float elapsed_{};
        bool started_{ false };
    };

} // namespace xone
