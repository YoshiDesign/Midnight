#pragma once
#include <mutex>
#include <vector>

// DEPRECATED (unless you really need a concurrent queue with locks)

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
            // O(n)! Dequeue was faster but caused realocation churn. Time this
            queue_.erase(queue_.begin());
            return true;
        }

        template <typename Fn>
        void drain(Fn&& fn)
        {
            {
                std::scoped_lock lock(mutex_);
                scratch_.swap(queue_);
            }

            for (auto& item : scratch_) {
                fn(item);
            }
            scratch_.clear();
        }

    private:
        std::mutex mutex_;
        std::vector<T> queue_;
        std::vector<T> scratch_; // reused across drain calls -- no per-frame alloc
    };
}