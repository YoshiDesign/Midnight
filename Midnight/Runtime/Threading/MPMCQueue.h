//#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <memory>

namespace mtools {

    namespace detail {

#if defined(__cpp_lib_hardware_interference_size)
        inline constexpr std::size_t cache_line_size =
            std::hardware_destructive_interference_size;
#else
        inline constexpr std::size_t cache_line_size = 64;
#endif

        inline bool is_power_of_two(std::size_t x) {
            return x != 0 && ((x & (x - 1)) == 0);
        }

    } // namespace detail


    template <typename T>
    class MPMCQueue {
    public:
        explicit MPMCQueue(std::size_t capacity)
            : capacity_(capacity),
            mask_(capacity - 1),
            buffer_(std::make_unique<Cell[]>(capacity))
        {
#ifdef M_DEBUG
            assert(detail::is_power_of_two(capacity_) &&
                "MPMCQueue capacity must be a power of two");
#endif
            for (std::size_t i = 0; i < capacity_; ++i) {
                buffer_[i].sequence.store(i, std::memory_order_relaxed);
            }

            enqueue_pos_.store(0, std::memory_order_relaxed);
            dequeue_pos_.store(0, std::memory_order_relaxed);
        }

        ~MPMCQueue() {
            // The queue must not be accessed concurrently during destruction.
            T item;
            while (try_pop(item)) {
                // Drain remaining live elements.
            }
        }

        MPMCQueue(const MPMCQueue&) = delete;
        MPMCQueue& operator=(const MPMCQueue&) = delete;

        MPMCQueue(MPMCQueue&&) = delete;
        MPMCQueue& operator=(MPMCQueue&&) = delete;

        template <typename U>
        bool try_push(U&& value) {

#ifdef M_DEBUG
            static_assert(std::is_constructible_v<T, U&&>,
                "T must be constructible from U&&");
#endif

            Cell* cell;
            std::size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

            for (;;) {
                cell = &buffer_[pos & mask_];

                std::size_t seq = cell->sequence.load(std::memory_order_acquire);
                intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

                if (diff == 0) {
                    // Slot is free for this producer generation.
                    if (enqueue_pos_.compare_exchange_weak(
                        pos,
                        pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) 
                    {
                        break;
                    }

                    // CAS failure updates pos with the observed value.
                    // Loop again.
                }
                else if (diff < 0) {
                    // The slot still belongs to an older generation.
                    // Queue is full, or at least this reservation cannot proceed.
                    return false;
                }
                else {
                    // Another producer moved ahead. Refresh our reservation guess.
                    pos = enqueue_pos_.load(std::memory_order_relaxed);
                }
            }

            // We exclusively own this cell now.
            construct_cell(cell, std::forward<U>(value));

            // Publish the written item to consumers.
            cell->sequence.store(pos + 1, std::memory_order_release);

            return true;
        }

        bool try_pop(T& out) {
            Cell* cell;
            std::size_t pos = dequeue_pos_.load(std::memory_order_relaxed);

            for (;;) {
                cell = &buffer_[pos & mask_];

                std::size_t seq = cell->sequence.load(std::memory_order_acquire);
                intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

                if (diff == 0) {
                    // Slot is full for this consumer generation.
                    if (dequeue_pos_.compare_exchange_weak(
                        pos,
                        pos + 1,
                        std::memory_order_relaxed,
                        std::memory_order_relaxed)) 
                    {
                        break;
                    }

                    // CAS failure updates pos with the observed value.
                    // Loop again.
                }
                else if (diff < 0) {
                    // Slot has not yet been published by a producer.
                    // Queue is empty, or at least no item is available here.
                    return false;
                }
                else {
                    // Another consumer moved ahead. Refresh our reservation guess.
                    pos = dequeue_pos_.load(std::memory_order_relaxed);
                }
            }

            // We exclusively own this full cell now.
            T* item = cell_ptr(cell);
            out = std::move(*item);
            item->~T();

            // Mark this slot free for the next producer generation.
            cell->sequence.store(pos + capacity_, std::memory_order_release);

            return true;
        }

        [[nodiscard]]
        std::size_t capacity() const {
            return capacity_;
        }

    private:
        struct alignas(detail::cache_line_size) Cell {
            std::atomic<std::size_t> sequence{ 0 };

            alignas(T) std::byte storage[sizeof(T)];
        };

        static T* cell_ptr(Cell* cell) {
            // Can these uses of std::launder instead be: `T* item = reinterpret_cast<T*>(&cell->storage);` ???
            return std::launder(reinterpret_cast<T*>(cell->storage));
        }

        // Const correct override
        static const T* cell_ptr(const Cell* cell) {
            return std::launder(reinterpret_cast<const T*>(cell->storage));
        }

        template <typename U>
        static void construct_cell(Cell* cell, U&& value) {
            ::new (static_cast<void*>(cell->storage)) T(std::forward<U>(value));
        }

    private:
        const std::size_t capacity_;
        const std::size_t mask_;

        std::unique_ptr<Cell[]> buffer_;

        alignas(detail::cache_line_size)
            std::atomic<std::size_t> enqueue_pos_{ 0 };

        alignas(detail::cache_line_size)
            std::atomic<std::size_t> dequeue_pos_{ 0 };
    };

} // namespace mtools