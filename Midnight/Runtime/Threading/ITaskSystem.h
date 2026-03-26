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

/*
* Currently, this implementation offers a lot of extensibility.
* E.g. , we could add priority queues, task stealing, or even a work-stealing scheduler.
* 
* Warning: This helping-wait pattern is great, but suceptible to over-eager graph expansion.
*          If multithreading a dependency graph of sorts, be sure to clearly define
*          your execution model, lazily if possible. The Terrain Generator is a great example of this
*          since stage requests execute upon prerequisite stage requests which must be completed first.
* 
*          Eagerly executing requests with any sort of dependency structure can lock up the application
*          or lead to frustrating bugs and ruin your dinner.
* 
* Eager: 
*   A request function recursively fans out and schedules the whole dependency tree immediately.
*
* Lazy:
*   A request function schedules only its own stage, and that stage requests its prerequisite 
*   when it actually executes. Clearly the more synchronized approach.
* 
* Notes: 
*   - Your pool is FIFO. Some tasks can be long, some short -> you can get load imbalance. 
*     It's fine for now, but later you might want work stealing.
* 
*   - The fact that we use shared_ptr for packaged_task means there’s some heap allocation per task. Not great, but not terrible at the moment.
*     See: MoveOnlyFunction.h for a dependency of the SBO approach, which would allow us to avoid any allocation for small tasks.
*     The optimization would be to have a custom task type that uses small-buffer optimization to avoid heap allocation for small tasks.
*     Reason being, the std::function requires a copyable type, so we can’t use unique_ptr.
*
*     - Current: FIFO Queue
*       Simple, lock-based
*       Can have load imbalance (one worker gets stuck with long task)
*     - Future Optimization: Work Stealing
*       Each worker gets its own deque
*       Idle workers steal from busy workers' back
*       Better load distribution, less contention
*
* An illustrative example of a task system:
* Thread Pool has 8 worker threads (Thread #0 through #7)
* Queue contains tasks from many chunks:Queue: 
*     [buildPoints(chunk A), buildHeights(chunk B), buildPoints(chunk C),
*      buildAllPoints(chunk A), buildTriangulation(chunk D), 
*      ...]        
*   | Thread #3 grabs: buildPoints(chunk A)     [x] completes
*   | Thread #3 grabs: buildHeights(chunk B)    [x] completes  
*   | Thread #3 grabs: buildAllPoints(chunk A)  [x] completes
*   | Thread #5 grabs: buildPoints(chunk C)     [x] completes
* Key insight: Threads are workers that pull tasks from a shared queue. They don't "own" chunks.
*
* [IMPORTANT] Every thread has its own ChunkArena instance.
*
* From a chunk's perspective:
* Chunk A generation stages (all different tasks):1. buildPoints(A)        
*   | Thread #3 executes2. buildAllPoints(A)      
*   | Thread #7 executes (waits for 9 neighbor Points)3. buildHeights(A)        
*   | Thread #3 executes (happens to grab it again)4. buildTriangulation(A)  
*   | Thread #1 executes5. buildErosion(A)        
*   | Thread #5 executes6. buildFinalMesh(A)      
*   | Thread #2 executes
* Each stage is a separate task. Any available thread can grab any task.
*/


inline thread_local int g_waitDepth = 0;
namespace aveng {
    /* The underlying abstract class, our "contract" which the threadpool will operate according to */
    class ITaskSystem {
    public:
        // Note: If the destructor is not virtual, ~ThreadPoolTaskSystem() won't run correctly when deleting via ITaskSystem*
        virtual ~ITaskSystem() = default;

        template<class Fn>
        auto submit(Fn&& fn) -> std::shared_future<decltype(std::declval<Fn>()())> {
            using R = decltype(std::declval<Fn>()());
            // using R = std::invoke_result_t<Fn&>; -- If we need a more general pattern with std::invoke(fn)

            // shared_ptr is a convenience tradeoff: simpler queue + lifetime, but some heap allocation / refcounting
            /*
            * We can remove it if the queue can store move-only jobs 
            * (e.g. a custom move-only function wrapper instead of std::function). 
            * Then we can move the packaged_task into the job and avoid shared_ptr overhead.
            */
            auto task = std::make_shared<std::packaged_task<R()>>(std::forward<Fn>(fn));
            std::shared_future<R> fut = task->get_future().share();
            enqueue([task]() { (*task)(); });
            return fut;
        }

        // A more "modern" approach to `submit` using invoke_result_t
        //template<class Fn>
        //auto submit(Fn&& fn) -> std::shared_future<std::invoke_result_t<Fn&>> {
        //    using R = std::invoke_result_t<Fn&>;
        //    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<Fn>(fn));
        //    auto fut = task->get_future().share();
        //    enqueue([task] { (*task)(); });
        //    return fut;
        //}


        // Helping wait: avoids deadlocks when workers wait on other tasks.
        template<class T>
        T wait(const std::shared_future<T>& fut) {

            //++g_waitDepth;
            //if (g_waitDepth > 1) {
            //    std::printf("[WAIT] tid=%u depth=%d%s\n",
            //        (unsigned)(std::hash<std::thread::id>{}(std::this_thread::get_id()) % 10000),
            //        g_waitDepth,
            //        g_waitDepth >= 4 ? " *** DEEP ***" : g_waitDepth >= 8 ? " *** EVEN DEEPER ***" : g_waitDepth >= 16 ? " *** SUPER DEEP ***" : "");
            //}
            //struct Scope { ~Scope() { --g_waitDepth; } } scope;

            while (fut.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                // Always try to run a job.
                if (!try_run_one_job()) {
                    // Nothing to do right now: yield.
                    std::this_thread::yield();
                }
            }
            // The fact that this returns a call to .get() has
			// a lot of useful implications. It's also tied to the fact that we're using shared_future.
            return fut.get();
        }

    protected:
        virtual void enqueue(std::function<void()> job) = 0;
        virtual bool try_run_one_job() = 0; // returns true if ran a job
    };

    /*
     * Thread Pool - a basic queue + worker threads
     */
    class ThreadPoolTaskSystem final : public ITaskSystem {
    public:
        explicit ThreadPoolTaskSystem(uint32_t threadCount);

        ~ThreadPoolTaskSystem() override;

        uint16_t nThreads() { return nThreads_; }

    private:

      /*
		* Enqueues a job to the queue.
        * Locks the queue and pushes the job.
        * Wakes up one sleeping worker thread.
        * We lock the queue because multiple threads can call submit() concurrently, 
        * and workers concurrently pop jobs.
       */
        void enqueue(std::function<void()> job) override;

      /* 
        * The "helping wait" hook 
        * It tries to take a job immediately (without sleeping).
        * If one exists, run it on the current thread.
        * This is what allows wait() to "help" the system progress.
       */
        bool try_run_one_job() override;

		// A standard "graceful shutdown" pattern for thread pools.
        void start(uint32_t threadCount);
        void stop();

    private:
        std::mutex m_;
        std::condition_variable cv_;
        std::deque<std::function<void()>> q_;
        std::vector<std::thread> workers_;
        std::atomic<bool> stopping_{ false };
        uint16_t nThreads_{0}; // Access for MT'd stages to adjust concurrency

		// This allows us to detect if we're on a worker thread. You set it inside of worker threads.
		// Currently unused, but this would allow us to implement features like:
        // "if we're on a worker thread, run the job immediately instead of enqueueing".
        // Or... If you call wait() on a worker thread -> help-run jobs.
        // Or... If you call wait() on the main thread -> either block or help - run lightly.
        static thread_local bool workerFlag_;
    };

}