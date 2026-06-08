#pragma once
#include <atomic>
#include "Runtime/Memory/Arena.h"

#if defined(__cpp_lib_hardware_interference_size)
	inline constexpr std::size_t cache_line_size =
		std::hardware_destructive_interference_size;
#else
	inline constexpr std::size_t cache_line_size = 64;
#endif

/* Chase-Lev Deque */

namespace mtools {

	template <typename T>
	class CLDeque
	{
	public:
		CLDeque(size_t capacity, Arena* arena) : _capacity(capacity), mask(_capacity - 1) {
			assert((_capacity & mask) == 0 && "DeQueue capacity must be a pwr of 2");
			buffer = ArenaAlloc(arena, capacity);
		}
		~CLDeque() {
			DestroyArena(arena);
		}

		// Owner operation
		bool push_bottom(T item) {
			// load bottom
			uint64_t bot = m_bottom.load(std::memory_order_relaxed);

			// load top
			uint64_t top = top.load(std::memory_order_acquire);

			if (bot - top >= _capacity) {
				// Full
				return false;
			}

			// Write. No atomic necessary to write to buffer, it's private
			buffer[bot & mask] = item;

			// increment, sync.
			m_bottom.store(bot + 1, std::memory_order_relaxed);

			return true;
		}

		// owner
		bool pop_bottom(T* out) {
		
			// load bottom
			uint64_t bot = m_bottom.load(std::memory_order_relaxed);

			// load top
			uint64_t top = m_top.load(std::memory_order_acquire);

			if (top >= m_bottom) {
				// Empty
				return false;
			}

			// Make your claim
			bot--;
			m_bottom.store(bot, std::memory_order_relaxed);

			// Sync the ship - make sure we see the latest
			std::atomic_thread_fence(std::memory_order_seq_cst);

			// Check - reload top to see if we raced (but relaxed this time)
			top = m_top.load(std::memory_order_relaxed);

			if (top <= bot) { // 
				T item = buffer[bot & mask];
				if (top == bot) {
					if (!top.compare_exchange_strong(top, top+1,
						std::memory_order_seq_cst, std::memory_order_relaxed)) {
						// Fail - A thief stole the last item. Reset the bottom and bail.
						m_bottom.store(bot - 1, std::memory_order_relaxed);
						return false;
					}
					// Success - Deque is now empty top == bottom + 1. Restore bottom == top
					m_bottom.store(bot + 1, std::memory_order_relaxed);
				}
				*out = item;
			}
			else { // Bad - Thief stole item after bottom decremented. The deque is empty, restore bottom and fail
				m_bottom.store(bot + 1, std::memory_order_relaxed);
				return false;
			}

		}	

		// Thief operation
		bool steal_top(T* out) {
			uint64_t top = m_top.load(std::memory_order_acquire);

			// Sync the ship - make sure we see the latest
			std::atomic_thread_fence(std:::memory_order_seq_cst);

			uint64_t bot = m_bottom.load(std::memory_order_acquire);

			if (top < bot) { // There is at least 1 item, guaranteed
				T item = buffer[top & mask];
				// CAS - if the item is in contention, only 1 thread wins.
				if (m_top.compare_exchange_strong(top, top + 1, 
					std::memory_order_seq_cst, std::memory_order_relaxed)) {
					// Success - steal item
					*out = item;
					return true;
				}
			}
			return false;
		}

	private:
		// Initialization order matters for these 2 since I'm using them immediately in the ctor
		uint64_t _capacity{ };
		uint64_t mask{ 0 };

		alignas(std::hardware_destructive_interference_size)
			std::atomic<uint64_t> m_bottom{ 0 };
		alignas(std::hardware_destructive_interference_size)
			std::atomic<uint64_t> m_top{ 0 };

		T* buffer{ };
	};
}