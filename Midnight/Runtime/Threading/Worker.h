#pragma once
#include <mutex>
#include <random>
#include <atomic>
#include <condition_variable>
#include "Runtime/Threading/Types.h"
#include "Runtime/Threading/CLDeque.h"

namespace mtools {

	struct Worker {

		void run();

		CLDeque<Job> dq_;
		std::mt19937 victim_rng;

		std::mutex sleepMutex;
		std::condition_variable cv;
		std::atomic<bool> sleeping{ false };

	};

}