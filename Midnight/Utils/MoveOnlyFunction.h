#pragma once
#include <utility>

/*
    This is here for educational purposes.

*/

namespace aveng {
    class MoveOnlyFunction {
    public:
        MoveOnlyFunction() noexcept = default;

        template<class F>
        MoveOnlyFunction(F&& f) {
            emplace<std::decay_t<F>>(std::forward<F>(f));
        }

        MoveOnlyFunction(MoveOnlyFunction&& other) noexcept {
            move_from(std::move(other));
        }

        MoveOnlyFunction& operator=(MoveOnlyFunction&& other) noexcept {
            if (this != &other) {
                reset();
                move_from(std::move(other));
            }
            return *this;
        }

        MoveOnlyFunction(const MoveOnlyFunction&) = delete;
        MoveOnlyFunction& operator=(const MoveOnlyFunction&) = delete;

        ~MoveOnlyFunction() { reset(); }

        void operator()() {
            // You can assert(op_ != nullptr) if you prefer.
            op_->call(ptr());
        }

        explicit operator bool() const noexcept { return op_ != nullptr; }

    private:
        // Tune this. 48/64 bytes is common. Pick based on your typical jobs.
        static constexpr std::size_t SBO_SIZE = 64;
        static constexpr std::size_t SBO_ALIGN = alignof(std::max_align_t);

        using Storage = std::aligned_storage_t<SBO_SIZE, SBO_ALIGN>;

        struct Ops {
            void (*call)(void*) noexcept;
            void (*destroy)(void*) noexcept;
            void (*move)(void* src, void* dst) noexcept; // move-construct into dst, destroy src
        };

        template<class F>
        static void call_impl(void* p) noexcept { (*static_cast<F*>(p))(); }

        template<class F>
        static void destroy_impl(void* p) noexcept { static_cast<F*>(p)->~F(); }

        template<class F>
        static void move_impl(void* src, void* dst) noexcept {
            // Move-construct into dst, then destroy src
            new (dst) F(std::move(*static_cast<F*>(src)));
            destroy_impl<F>(src);
        }

        template<class F>
        static const Ops* ops_for() noexcept {
            static constexpr Ops ops = {
                &call_impl<F>,
                &destroy_impl<F>,
                &move_impl<F>,
            };
            return &ops;
        }

        static constexpr bool fits_inline(std::size_t size, std::size_t align) noexcept {
            return size <= SBO_SIZE && align <= SBO_ALIGN;
        }

        void* ptr() noexcept {
            return is_heap_ ? heap_ : static_cast<void*>(&storage_);
        }
        const void* ptr() const noexcept {
            return is_heap_ ? heap_ : static_cast<const void*>(&storage_);
        }

        void reset() noexcept {
            if (!op_) return;
            if (is_heap_) {
                // Heap object lives at heap_. destroy() expects pointer to object.
                op_->destroy(heap_);
                ::operator delete(heap_, std::align_val_t(SBO_ALIGN));
                heap_ = nullptr;
            }
            else {
                op_->destroy(&storage_);
            }
            op_ = nullptr;
            is_heap_ = false;
        }

        void move_from(MoveOnlyFunction&& other) noexcept {
            op_ = other.op_;
            is_heap_ = other.is_heap_;
            if (!op_) return;

            if (other.is_heap_) {
                heap_ = other.heap_;
                other.heap_ = nullptr;
            }
            else {
                op_->move(&other.storage_, &storage_);
            }

            other.op_ = nullptr;
            other.is_heap_ = false;
        }

        template<class F, class... Args>
        void emplace(Args&&... args) {
            constexpr bool inline_ok =
                fits_inline(sizeof(F), alignof(F)) &&
                std::is_nothrow_move_constructible_v<F>;

            op_ = ops_for<F>();

            if constexpr (inline_ok) {
                is_heap_ = false;
                new (&storage_) F(std::forward<Args>(args)...);
            }
            else {
                is_heap_ = true;
                // aligned allocate; you can simplify if you don’t care about over-alignment.
                void* mem = ::operator new(sizeof(F), std::align_val_t(std::max<std::size_t>(SBO_ALIGN, alignof(F))));
                heap_ = mem;
                new (heap_) F(std::forward<Args>(args)...);
            }
        }

    private:
        const Ops* op_ = nullptr;
        bool is_heap_ = false;

        union {
            Storage storage_;
            void* heap_;
        };
    };

}