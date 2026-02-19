#include "HolyShip.h"
#include <Runtime/Play/GameContext.h>
#include <Module/Procgen/Types.h>

namespace xone {

    void HolyShip::onEnter()
    {

    }

    void HolyShip::update(const TickContext& ctx, const aveng::GameServices& services) {
        // Pretend the user pressed "Start"
        elapsed_ += ctx.dt;
        if (elapsed_ > 1.0f && !started_) {
            started_ = true;
            play_.requestPlay("holyship");
            services.terrain.generateChunks(aveng::ChunkCoord{ 0,0 }, 2, 2);
        }
        
    }
	
}