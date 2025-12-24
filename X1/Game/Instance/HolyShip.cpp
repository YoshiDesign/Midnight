#include "HolyShip.h"

namespace xone {

	void HolyShip::update(const TickContext& ctx) {
        // Pretend the user pressed "Start"
        elapsed_ += ctx.dt;
        if (elapsed_ > 1.0f && !started_) {
            started_ = true;
            play_.requestPlay("holyship");
        }
        
    }
	
}