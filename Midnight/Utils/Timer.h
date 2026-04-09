#pragma once

#include <chrono>

namespace aveng {
    class Timer {
    public:
        Timer() {};
        ~Timer();
        void start();
        /* stops timer and returns millisconds since start, in microsecond resolution */
        float stop();

    private:
        bool mRunning = false;
        std::chrono::time_point<std::chrono::steady_clock> mStartTime{};
    };

}