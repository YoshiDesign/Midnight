#pragma once
#include <unordered_map>

class GameData;
class EngineDevice;
class AvengWindow;
class Renderer;
class AvengAppObject;

namespace aveng {
    struct SystemContext {
        GameData* game_data = nullptr;
        EngineDevice* device = nullptr;
        AvengWindow* window = nullptr;
        Renderer* renderer = nullptr;
        std::unordered_map<unsigned int, AvengAppObject>* appObjects = nullptr;
    };
}