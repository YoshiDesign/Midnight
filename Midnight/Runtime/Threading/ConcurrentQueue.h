#pragma once
#include <mutex>
#include <deque>
namespace mtools {
    template <typename T>
    class ConcurrentQueue
    {
    public:
        void push(T value)
        {
            std::scoped_lock lock(mutex_);
            queue_.push_back(std::move(value));
        }

        bool tryPop(T& out)
        {
            std::scoped_lock lock(mutex_);
            if (queue_.empty()) return false;
            out = std::move(queue_.front());
            queue_.pop_front();
            return true;
        }
        
        /* This doesn't need to be templated and can hinder perf */
        template <typename Fn>
        void drain(Fn&& fn)
        {
            // Swap the queue to a local w/ a lock
            std::deque<T> local;
            {
                std::scoped_lock lock(mutex_);
                local.swap(queue_);
            }

            // `item` is our completed terrain chunk (Terrain Generator)
            for (auto& item : local) {
                fn(item);
            }
        }

    private:
        std::mutex mutex_;
        std::deque<T> queue_;
    };
}