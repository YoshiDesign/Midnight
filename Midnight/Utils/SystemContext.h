#pragma once
#include <unordered_map>
#include "Core/data.h"
#include "Core/app_object.h"
#include "Core/aveng_window.h"
#include "Core/Renderer/Renderer.h"
#include "System/Camera/aveng_camera.h"

namespace aveng {
    struct SystemContext {
        EngineDevice* device = nullptr;
        AvengWindow* window = nullptr;
        AvengCamera* camera = nullptr;
        Renderer* renderer = nullptr;
        GameData* game_data = nullptr;
        AvengAppObject::Map* appObjects = nullptr;
    };
}