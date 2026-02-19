#pragma once
#include <cstdio>
#include "GameRegistry.h"
#include "Runtime/Facade/SceneFacade.h"
#include "Runtime/Play/GameContext.h"
#include "Runtime/Play/Controller/TerrainController.h"
namespace xone {

    class PlayManager {
    public:
        explicit PlayManager(const GameRegistry& registry)
            : registry_(registry) {
        }

        // Request a transition to another game/sim (safe to call from anywhere)
        void requestPlay(std::string_view id) {
            pendingId_ = std::string(id);
            std::cout << "Pending Game ID: " << pendingId_ << std::endl;
        }

        std::string_view activeId() const { return activeId_; }
        IGameInstance* active() { return active_.get(); }
        const IGameInstance* active() const { return active_.get(); }

        // Apply game/sim transitions + update current game
        // TickContext includes input state
        void update(const TickContext& ctx, const aveng::GameServices& services) {

            // Apply pending playable transitions
            if (!pendingId_.empty() && pendingId_ != activeId_) {
                switchTo(pendingId_);
                pendingId_.clear();
            }

            if (active_) {
                
                // TODO - This might be tmp placement
                services.terrain.setFrameIndex(ctx.frameIndex);

                active_->update(ctx, services);
            }

        }

        void render(const RenderContext& ctx) {
            if (active_) {
                active_->render(ctx);
            }
        }

    private:

        // Return the executed factory function from our game registry
        void switchTo(std::string id) {
            // Tear down old
            if (active_) {
                // Hook
                active_->onExit();
                // Destroy the current game/scene ptr
                active_.reset();
            }

            // Create new from factory
            auto created = registry_.create(id);
            if (!created) {
                // You can replace with your assert/log system
                // For now: leave active_ null
                activeId_.clear();
                return;
            }

            active_ = std::move(created);
            activeId_ = id;
            active_->onEnter();
        }

    private:
        const GameRegistry& registry_;
        std::unique_ptr<IGameInstance> active_;
        std::string activeId_;
        std::string pendingId_;
    };

}
