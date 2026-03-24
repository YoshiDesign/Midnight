#include "HolyShip.h"
#include <Runtime/Play/GameContext.h>
#include <Module/Procgen/Types.h>

namespace xone {

    void HolyShip::onEnter()
    {
        std::printf("Enter The Gecko...\n");
        game_start = true;
        terrainStream_.reset();
    }

    void HolyShip::update(const TickContext& ctx) {
        terrainStream_.update(ctx.camera, ctx.frameIndex);
        // Pretend the user pressed "Start"
        //elapsed_ += ctx.dt;
        //if (elapsed_ > 0.25f && !game_start) {
        //    play_.requestPlay("holyship");
        //    game_start = true;
        //}
        
    }
	
}