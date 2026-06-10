#pragma once
#include <vector>
#include "Runtime/Threading/Types.h"
#include "Runtime/Threading/MPMCQueue.h"
#include "Runtime/Threading/Job.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
namespace mtools {
    class Scheduler {
    public:
        static constexpr size_t GlobalQueueCapacity = 4096;

        Scheduler() = default;

        Scheduler(const Scheduler&) = delete;
        Scheduler& operator=(const Scheduler&) = delete;

        ~Scheduler() {
            shutdown();
        }

        bool start(uint32_t workerCount) {
            if (running_.load(std::memory_order_acquire)) {
                return false;
            }

            stopping_.store(false, std::memory_order_release);
            running_.store(true, std::memory_order_release);

            workers_.reserve(workerCount);

            for (uint32_t i = 0; i < workerCount; ++i) {
                workers_.emplace_back([this, i] {
                    workerMain(i);
                });
            }

            return true;
        }

        void shutdown() {
            bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);

            if (!wasRunning) {
                return;
            }

            stopping_.store(true, std::memory_order_release);

            {
                std::lock_guard<std::mutex> lock(waitMutex_);
                wakeCounter_.fetch_add(1, std::memory_order_relaxed);
            }

            waitCv_.notify_all();

            for (std::thread& worker : workers_) {
                if (worker.joinable()) {
                    worker.join();
                }
            }

            workers_.clear();
        }

        bool submit(Job&& job) {
            if (!running_.load(std::memory_order_acquire)) {
                return false;
            }

            bool pushed = globalQueue_.try_push(std::move(job));

            if (pushed) {
                wakeOne();
            }

            return pushed;
        }

        template <typename Payload>
        bool submit(void (*fn)(JobContext&, const Payload&), Payload payload) {
            return submit(Job::make(fn, std::move(payload)));
        }

    private:
        void workerMain(uint32_t workerIndex) {
            JobContext ctx;
            ctx.scheduler = this;
            ctx.workerIndex = workerIndex;

            while (!stopping_.load(std::memory_order_acquire)) {
                Job job;

                if (globalQueue_.try_pop(job)) {
                    job.run(ctx);
                    continue;
                }

                waitForWork();
            }

            // Optional drain:
            //
            // If you want shutdown to finish already-submitted jobs, drain here.
            // If you want shutdown to abandon pending jobs, remove this loop.
            Job job;

            while (globalQueue_.try_pop(job)) {
                job.run(ctx);
            }
        }

        void wakeOne() {
            wakeCounter_.fetch_add(1, std::memory_order_release);
            waitCv_.notify_one();
        }

        void waitForWork() {
            std::unique_lock<std::mutex> lock(waitMutex_);

            uint64_t observed = wakeCounter_.load(std::memory_order_acquire);

            waitCv_.wait(lock, [&] {
                return stopping_.load(std::memory_order_acquire) ||
                    wakeCounter_.load(std::memory_order_acquire) != observed;
            });
        }

    private:
        MPMCQueue<Job> globalQueue_{ GlobalQueueCapacity };

        std::vector<std::thread> workers_;

        std::atomic<bool> running_{ false };
        std::atomic<bool> stopping_{ false };

        std::mutex waitMutex_;
        std::condition_variable waitCv_;
        std::atomic<uint64_t> wakeCounter_{ 0 };
    };
}