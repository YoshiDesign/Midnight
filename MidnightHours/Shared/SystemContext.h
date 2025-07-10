#pragma once



namespace aveng {
    struct SystemContext {
        GameData* game_data = nullptr;
        EngineDevice* device = nullptr;
        AvengWindow* window = nullptr;
        Renderer* renderer = nullptr;
        GameData* gameData = nullptr;
        // AvengAppObject::Map* appObjects = nullptr;
    };
}