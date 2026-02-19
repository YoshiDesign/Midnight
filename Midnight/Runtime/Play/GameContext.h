#pragma once
namespace aveng {

    class TerrainController;
    class DebugController;

    struct GameServices {
        TerrainController& terrain;
        DebugController& debug;
    };

}