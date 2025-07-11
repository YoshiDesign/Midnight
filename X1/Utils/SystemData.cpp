#include "SystemData.h"
#include <cassert>

namespace aveng {

    SystemData::SystemData(
        EngineDevice& device,
        AvengWindow& aveng_window,
        AvengCamera& camera,
        Renderer& renderer,
        GameData& gameData,
        AvengAppObject::Map& appObjects
    ) : 
        device(device),
        aveng_window(aveng_window),
        camera(camera),
        renderer(renderer),
        gameData(gameData),
        appObjects(appObjects)
    {
        createContext();
    }

    SystemData::~SystemData()
    {
    }

    void SystemData::createContext()
    {

        context.game_data = &gameData;
        context.device = &device;
        context.window = &aveng_window;
        context.renderer = &renderer;
        context.appObjects = &appObjects;
        context.camera = &camera;

        std::cout << "CHECKING FOR NULLS:\t" 
            << context.game_data->modPI << "\n"
            << context.device->properties.limits.maxVertexInputBindings << std::endl;
    }

    SystemContext& SystemData::systemContext()
    {
        // Runtime check ((c)asserts are automatically stripped in release builds)
        assert(context.device && "SystemContext: device is nullptr");
        assert(context.window && "SystemContext: window is nullptr");
        assert(context.renderer && "SystemContext: renderer is nullptr");
        assert(context.game_data && "SystemContext: game_data is nullptr");
        assert(context.camera && "SystemContext: camera is nullptr");
        assert(context.appObjects && "SystemContext: appObjects is nullptr");
        return context;
    }
}
