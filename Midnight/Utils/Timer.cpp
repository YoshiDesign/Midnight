#include "Utils/Timer.h"

namespace aveng {

    void Timer::start() {
        if (mRunning) {
            std::printf("%s error: timer already running\n", __FUNCTION__);
            return;
        }

        mRunning = true;
        mStartTime = std::chrono::steady_clock::now();
    }

    float Timer::stop() {
        if (!mRunning) {
            std::printf("%s error: timer not running\n", __FUNCTION__);
            return 0;
        }
        mRunning = false;

        auto stopTime = std::chrono::steady_clock::now();
        float timerMilliSeconds = std::chrono::duration_cast<std::chrono::microseconds>(stopTime - mStartTime).count() / 1000.0f;

        return timerMilliSeconds;
    }

}