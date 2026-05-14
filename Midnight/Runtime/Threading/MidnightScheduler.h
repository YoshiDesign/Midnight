#pragma once
#include <vector>
#include "Runtime/Threading/Types.h"
#include "Runtime/Threading/MPMCInjector.h"

/**
* Keep the scheduler as dumb as possible, it should only
* care about submitting jobs, not understanding task dependencies
* or knowing where boundaries occur between systems.
*/

namespace mtools {
	struct MidnightScheduler 
	{

        std::vector<Worker> workers_;
        std::vector<std::thread> threads_;
        MPMCInjector<Job> injector_;        // Global injector queue

        void submit(Job job);
        void submitMany(Job* jobs, uint32_t count);

        void start(uint32_t workerCount);
        void stop();

    private:
        void workerLoop(uint32_t workerId);
        bool findWork(uint32_t workerId, Job& out);
        bool trySteal(uint32_t thiefId, Job& out);

	};
}