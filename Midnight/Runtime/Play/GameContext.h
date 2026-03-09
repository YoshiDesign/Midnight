#pragma once
namespace aveng {

    class TerrainController;
    class DebugController;
    class SceneFacade;

    struct GameServices {

        // This operates our procedural terrain generation system
        TerrainController& terrain;

        // State information
        DebugController& debug;

        // The Scene can load models, spawn instances, load textures,
        // accept level payloads (cluster of information for assets that we need to load a level).
        // It is also accessible directly from the Editor.
        SceneFacade& scene; 
    };

}