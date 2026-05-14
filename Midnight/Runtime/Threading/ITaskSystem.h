#pragma once
#include <future>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
#include <cassert>

/*
    SUPERCALIDEPRECATED
*/

/* 
* FIFO thread pool with submit/enqueue and helping-wait.
*
* Concurrency policy (terrain generation and beyond):
*   - Main thread may block on stage futures.
*   - Worker threads should NOT block on chunk-stage futures.
*     Workers check readiness via isReady() and re-enqueue if deps aren't done.
*   - Leaf-level parallel compute (e.g. hydraulic erosion sub-batches) may still
*     use helping-wait internally since those are self-contained.
*
* Notes:
*   - FIFO queue; work-stealing is a future optimization.
*   - shared_ptr<packaged_task> per submit -- small heap cost, acceptable for now.
*   - Every worker thread has its own ChunkArena (thread-local scratch).
*/

namespace aveng {

    using TaskFn = std::function<void()>;

    class ITaskSystem {
    public:
        virtual ~ITaskSystem() = default;

        // --- Submission ---

        virtual void enqueue(TaskFn fn) = 0;

        template<class Fn>
        auto submit(Fn&& fn) -> std::shared_future<decltype(std::declval<Fn>()())> {
            using R = decltype(std::declval<Fn>()());
            auto task = std::make_shared<std::packaged_task<R()>>(std::forward<Fn>(fn));
            std::shared_future<R> fut = task->get_future().share();
            enqueue([task]() { (*task)(); });
            return fut;
        }

        // --- Worker identity ---

        virtual bool isWorkerThread() const noexcept = 0;

        // --- Controlled helping ---

        virtual bool tryRunOneJob() = 0;

        // --- Waiting ---
        // External (non-worker) threads block normally.
        // Worker threads get a debug assert -- the new policy is to re-enqueue instead of block.
        // The helping-wait fallback remains for the transition period and for leaf-level compute.

        template<class T>
        T wait(const std::shared_future<T>& fut) {
            if (!isWorkerThread()) {
                fut.wait();
                return fut.get();
            }

#ifndef NDEBUG
            // During migration this will fire on any unconverted stage chains.
            // Convert the caller to the re-enqueue pattern, then this goes away.
            assert(false && "Worker thread called blocking wait(). Convert to re-enqueue pattern.");
#endif
            // Helping-wait fallback (safe for leaf-level parallel compute)
            while (fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                if (!tryRunOneJob()) {
                    std::this_thread::yield();
                }
            }
            return fut.get();
        }

        virtual uint32_t workerCount() const noexcept = 0;
    };

    /*
     * Thread Pool -- FIFO queue + worker threads
     */
    class ThreadPoolTaskSystem final : public ITaskSystem {
    public:
        explicit ThreadPoolTaskSystem(uint32_t threadCount);
        ~ThreadPoolTaskSystem() override;

        void enqueue(TaskFn fn) override;
        bool isWorkerThread() const noexcept override;
        bool tryRunOneJob() override;
        uint32_t workerCount() const noexcept override { return nThreads_; }

        uint16_t nThreads() { return nThreads_; }

    private:
        void start(uint32_t threadCount);
        void stop();

        std::mutex m_;
        std::condition_variable cv_;
        std::deque<TaskFn> q_;
        std::vector<std::thread> workers_;
        std::atomic<bool> stopping_{ false };
        uint16_t nThreads_{0};

        static thread_local bool workerFlag_;
    };

}
