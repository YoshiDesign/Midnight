#pragma once
#include "Runtime/Play/GameContext.h"
#include "Runtime/Play/IGameInstance.h"
#include "Runtime/Play/PlayManager.h"
#include "Game/Module/Procgen/TerrainStreamer.h"

namespace xone {

    // Not exactly a fan of abstract base classes here
    // But it's tightly coupled to our registry and PlayManager at the moment.
    class HolyShip final : public IGameInstance {
    public:
        explicit HolyShip(PlayManager& play, const aveng::GameServices& gs) 
            : play_(play), 
            gs_(gs), 
            terrainStream_(gs_.terrain, aveng::TerrainStreamPolicy{}) 
        {}

        std::string name() const override { return "HolyShip"; }

        void onEnter() override;

        void onExit() override {
            std::cout << "[HolyShip] Exit\n";
        }

        void update(const TickContext& ctx) override;

        void render(const RenderContext&) override {
            // draw game
        }

    private:
        float elapsed_{};
        bool game_start{ false };

        PlayManager& play_;
        const aveng::GameServices& gs_;
        TerrainStreamer terrainStream_;
    };

} 
