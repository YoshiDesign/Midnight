#include "ITaskSystem.h"
#include "Runtime/Threading/Scratch.h"
#include "Runtime/Memory/ChunkArena.h"
#include <iostream>
namespace aveng {

	thread_local bool ThreadPoolTaskSystem::workerFlag_ = false;

    ThreadPoolTaskSystem::ThreadPoolTaskSystem(uint32_t threadCount) {
        std::cout << __FUNCTION__ << "Starting ThreadPoolTaskSystem with " << threadCount << " threads." << std::endl;
        start(threadCount);
    }

    ThreadPoolTaskSystem::~ThreadPoolTaskSystem() {
        stop();
    }

    // The worker loop
    void ThreadPoolTaskSystem::start(uint32_t threadCount){
        stopping_.store(false, std::memory_order_release);
        workers_.reserve(threadCount);

        // Create worker threads. Each thread runs a loop where it waits for jobs and executes them.
        for (uint32_t i = 0; i < threadCount; ++i) {
            workers_.emplace_back([this] {
                workerFlag_ = true;

                // Prepare a scratch memory resource for every 
                tlsScratchArena().reserve(8 * 1024 * 1024); // 8MB per thread

                for (;;) {
                    std::function<void()> job;
                    {
                        std::unique_lock<std::mutex> lock(m_);
                        cv_.wait(lock, [&] { return stopping_.load(std::memory_order_acquire) || !q_.empty(); });
                        if (stopping_.load(std::memory_order_acquire) && q_.empty()) return;
                        job = std::move(q_.front());
                        q_.pop_front();
                    }
                    job();
                }
            });
        }
    }

    void ThreadPoolTaskSystem::stop() {
        stopping_.store(true, std::memory_order_release);
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
        workers_.clear();
    }

    bool ThreadPoolTaskSystem::try_run_one_job() {
        std::function<void()> job;
        {
            std::lock_guard<std::mutex> lock(m_);
            if (q_.empty()) return false;
            job = std::move(q_.front());
            q_.pop_front();
        }
        job();
        return true;
    }

    void ThreadPoolTaskSystem::enqueue(std::function<void()> job) {
        {
            std::lock_guard<std::mutex> lock(m_);
            q_.push_back(std::move(job));
        }
        cv_.notify_one();
    }
    
}