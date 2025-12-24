#pragma once
#include "avpch.h"

#include "IGameInstance.h"

namespace xone {

    class GameRegistry {
    public:
        using Factory = std::function<std::unique_ptr<IGameInstance>()>;

        // Register a playable by id (ex: "menu", "main_game")
        void registerGame(std::string id, Factory factory) {
            std::cout << id << ": Loaded by Registry" << std::endl;
            factories_[std::move(id)] = std::move(factory);
        }

        bool has(std::string_view id) const {
            return factories_.find(std::string(id)) != factories_.end();
        }

        std::unique_ptr<IGameInstance> create(std::string_view id) const {
            std::cout << "Registry Creating: " << std::string(id) << std::endl;
            auto it = factories_.find(std::string(id));
            if (it == factories_.end()) return nullptr;
            return (it->second)(); // Execute the registered factory function.
        }

    private:
        std::unordered_map<std::string, Factory> factories_;
    };

}
