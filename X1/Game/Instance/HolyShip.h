#pragma once
#include "Runtime/Play/GameContext.h"
#include "Runtime/Play/IGameInstance.h"
#include "Runtime/Play/PlayManager.h"

namespace xone {

    class HolyShip final : public IGameInstance {
    public:
        explicit HolyShip(PlayManager& play) : play_(play) {}

        std::string name() const override { return "HolyShip"; }

        void onEnter() override;

        void onExit() override {
            std::cout << "[HolyShip] Exit\n";
        }

        void update(const TickContext& ctx, const aveng::GameServices& services) override;

        void render(const RenderContext&) override {
            // draw game
        }

    private:
        PlayManager& play_;
        float elapsed_{};
        bool started_{ false };
    };

} 
