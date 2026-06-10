#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
//#include <utility>
//#include <new>
#include <cstring>
namespace mtools {

    class Scheduler;

    struct JobContext {
        Scheduler* scheduler{ nullptr };
        uint32_t workerIndex{ 0 };
    };

    struct Job {
        using ExecuteFn = void(*)(JobContext&, const void*);
        using DestroyFn = void(*)(void*);

        static constexpr size_t PayloadSize = 64;
        static constexpr size_t PayloadAlign = 16;

        ExecuteFn execute{ nullptr };
        DestroyFn destroy{ nullptr };


        alignas(PayloadAlign) std::byte payload[PayloadSize];

        Job() = default;

        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;

        Job(Job&& other) noexcept {
            moveFrom(std::move(other));
        }

        Job& operator=(Job&& other) noexcept {
            if (this != &other) {
                reset();
                moveFrom(std::move(other));
            }

            return *this;
        }

        ~Job() {
            reset();
        }

        explicit operator bool() const {
            return execute != nullptr;
        }

        void run(JobContext& ctx) {
            execute(ctx, payload);
        }

        void reset() {
            if (destroy) {
                destroy(payload);
            }

            execute = nullptr;
            destroy = nullptr;
        }

        template <typename Payload>
        static Job make(void (*fn)(JobContext&, const Payload&), Payload payloadValue) {
            static_assert(sizeof(Payload) <= PayloadSize, "Payload too large for inline Job storage");
            static_assert(alignof(Payload) <= PayloadAlign, "Payload alignment too large for Job");
            static_assert(std::is_move_constructible_v<Payload>, "Payload must be move constructible");

            Job job;

            new (job.payload) Payload(std::move(payloadValue));

            job.execute = [](JobContext& ctx, const void* raw) {
                const Payload& payload = *static_cast<const Payload*>(raw);
                fn(ctx, payload);
            };

            if constexpr (!std::is_trivially_destructible_v<Payload>) {
                job.destroy = [](void* raw) {
                    static_cast<Payload*>(raw)->~Payload();
                };
            }
            else {
                job.destroy = nullptr;
            }

            return job;
        }

    private:
        void moveFrom(Job&& other) noexcept {
            execute = other.execute;
            destroy = other.destroy;

            memcpy(payload, other.payload, PayloadSize);

            other.execute = nullptr;
            other.destroy = nullptr;
        }
    };

}