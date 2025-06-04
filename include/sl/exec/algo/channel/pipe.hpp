//
// Created by usatiynyan.
//
// `pipe` is an SPSC(Single Producer Single Consumer) channel
// `pipe.in.send(...)` can be called multiple times, would block when there is no "Consumer".
// `Signal pipe.out.receive()` may invoke subscription multiple times.
// There's no guarantee that subscription to `pipe.out.receive()` will be called from "Producer" or "Consumer" thread.
//

#pragma once

#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/lifetime/finalizer.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>
#include <sl/meta/traits/unique.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, template <typename> typename Atomic>
struct pipe_storage {
    using result_type = meta::result<ValueT, ErrorT>;

    struct pipe_state {
        enum class tag { in, out };

        virtual ~pipe_state() = default;
        virtual tag get_tag() const& = 0;

        bool is_in() const& { return get_tag() == tag::in; }
        bool is_out() const& { return get_tag() == tag::out; }
    };

    struct in_type : pipe_state {
        pipe_state::tag get_tag() const& override { return pipe_state::tag::in; }

    public:
        in_type(meta::maybe<result_type> maybe_result, slot<meta::unit, meta::undefined>& in_slot)
            : maybe_result_{ std::move(maybe_result) }, in_slot_{ in_slot } {}

        void fulfill(slot<ValueT, ErrorT>& out_slot) noexcept {
            fulfill_slot(out_slot, std::move(maybe_result_));
            in_slot_.set_value(meta::unit{});
        }

    private:
        meta::maybe<result_type> maybe_result_;
        slot<meta::unit, meta::undefined>& in_slot_;
    };

    struct out_type : pipe_state {
        pipe_state::tag get_tag() const& override { return pipe_state::tag::out; }

    public:
        explicit out_type(slot<ValueT, ErrorT>* slot) : slot_{ slot } {}

        slot<ValueT, ErrorT>* get_slot() { return slot_; }

    private:
        slot<ValueT, ErrorT>* slot_;
    };

public:
    void emit_in(in_type* in) & {
        pipe_state* state = nullptr;
        if (state_.compare_exchange_strong(state, in, std::memory_order::release, std::memory_order::acquire)) {
            return;
        }
        DEBUG_ASSERT(state->is_out(), "only single producer");

        auto* slot = static_cast<out_type*>(state)->get_slot();
        DEBUG_ASSERT(slot != nullptr, "programmer error: empty slot in out_type");
        in->fulfill(*slot);
    }

    void register_out(out_type* out) & {
        pipe_state* const state = state_.exchange(out, std::memory_order::acq_rel);
        if (state == nullptr) {
            return;
        }
        DEBUG_ASSERT(state->is_in(), "only single consumer");

        in_type* const in_state = static_cast<in_type*>(state);
        in_state->fulfill(*out->get_slot());
    }

    void unregister_out(out_type* out) & {
        pipe_state* state = out;

        if (state_.compare_exchange_strong(state, nullptr, std::memory_order::release, std::memory_order::relaxed)) {
            return;
        }

        UNREACHABLE(state, "programmer error: unregistering twice or not registering at all");
    }

public: // refcount
    void incref() & {
        [[maybe_unused]] const std::uint32_t prev = refcount_.fetch_add(1, std::memory_order::relaxed);
        DEBUG_ASSERT(prev != 0);
    }

    void decref() & {
        if (refcount_.fetch_sub(1, std::memory_order::relaxed) == 1) {
            delete this;
        }
    }

private:
    // in and out refcounted upon init
    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> refcount_{ 2 };
    alignas(hardware_destructive_interference_size) Atomic<pipe_state*> state_{ nullptr };
};

} // namespace detail

template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] pipe_in : meta::finalizer<pipe_in<ValueT, ErrorT, Atomic>> {
    using storage_type = detail::pipe_storage<ValueT, ErrorT, Atomic>;
    using result_type = meta::result<ValueT, ErrorT>;

    struct [[nodiscard]] connection_type : meta::immovable {
        connection_type(
            meta::maybe<result_type> maybe_result,
            storage_type& storage,
            slot<meta::unit, meta::undefined>& slot
        )
            : in_{
                  /* .maybe_result = */ std::move(maybe_result),
                  /* .slot = */ slot,
              },
              storage_ref_{ storage } {}

        ~connection_type() noexcept {
            if (ASSUME_VAL(emitted_)) {
                storage_ref_.decref();
            }
        }

        void emit() && {
            emitted_ = true;
            storage_ref_.incref();
            storage_ref_.emit_in(&in_);
        }

    private:
        storage_type::in_type in_;
        storage_type& storage_ref_;
        bool emitted_ = false;
    };

    struct [[nodiscard]] signal_type : meta::unique {
        using value_type = meta::unit;
        using error_type = meta::undefined;

    public:
        constexpr signal_type(meta::maybe<result_type> maybe_result, storage_type* storage_ptr)
            : maybe_result_{ std::move(maybe_result) }, storage_ptr_{ storage_ptr } {}

        executor& get_executor() & { return exec::inline_executor(); }

        Connection auto subscribe(slot<value_type, error_type>& slot) && {
            storage_type* storage_ptr = std::exchange(storage_ptr_, nullptr);
            DEBUG_ASSERT(storage_ptr != nullptr);
            return connection_type{
                /* .maybe_result = */ std::move(maybe_result_),
                /* .storage = */ *storage_ptr,
                /* .slot = */ slot,
            };
        }

    private:
        meta::maybe<result_type> maybe_result_;
        storage_type* storage_ptr_;
    };

public:
    constexpr explicit pipe_in(storage_type* storage_ptr)
        : meta::finalizer<pipe_in>{ [](pipe_in& self) {
              DEBUG_ASSERT(nullptr != self.storage_ptr_);
              self.storage_ptr_->decref();
          } },
          storage_ptr_{ storage_ptr } {}

    constexpr Signal<meta::unit, meta::undefined> auto send(result_type result) & {
        return signal_type{
            /* .maybe_result = */ std::move(result),
            /* .storage_ptr = */ storage_ptr_,
        };
    }

    constexpr Signal<meta::unit, meta::undefined> auto close() && {
        return signal_type{
            /* .maybe_result = */ meta::null,
            /* .storage_ptr = */ storage_ptr_,
        };
    }

private:
    storage_type* storage_ptr_;
};

// should satisfy SomeSignal
template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] pipe_out : meta::finalizer<pipe_out<ValueT, ErrorT, Atomic>> {
    using storage_type = detail::pipe_storage<ValueT, ErrorT, Atomic>;

    struct [[nodiscard]] connection_type : meta::immovable {
        using out_type = typename storage_type::out_type;

    public:
        explicit connection_type(storage_type& storage, slot<ValueT, ErrorT>& slot)
            : out_{ &slot }, storage_ref_{ storage }  {
            DEBUG_ASSERT(out_.get_slot() != nullptr);
        }

        ~connection_type() noexcept {
            if (ASSUME_VAL(registered_)) {
                storage_ref_.unregister_out(&out_);
                storage_ref_.decref();
            }
        }

        void emit() && {
            registered_ = true;
            storage_ref_.incref();
            storage_ref_.register_out(&out_);
        }

    private:
        out_type out_;
        storage_type& storage_ref_;
        bool registered_ = false;
    };

    struct [[nodiscard]] signal_type : meta::unique {
        using value_type = ValueT;
        using error_type = ErrorT;

    public:
        constexpr explicit signal_type(storage_type* storage_ptr) : storage_ptr_{ storage_ptr } {}

        executor& get_executor() & { return exec::inline_executor(); }

        Connection auto subscribe(slot<value_type, error_type>& slot) && {
            storage_type* storage_ptr = std::exchange(storage_ptr_, nullptr);
            DEBUG_ASSERT(storage_ptr != nullptr);
            return connection_type{
                /* .storage = */ *storage_ptr,
                /* .slot = */ slot,
            };
        }

    private:
        storage_type* storage_ptr_;
    };

public:
    constexpr explicit pipe_out(storage_type* storage_ptr)
        : meta::finalizer<pipe_out>{ [](pipe_out& self) {
              DEBUG_ASSERT(nullptr != self.storage_ptr_);
              self.storage_ptr_->decref();
          } },
          storage_ptr_{ storage_ptr } {}

    constexpr Signal<ValueT, ErrorT> auto receive() & { return signal_type{ /* .storage_ptr = */ storage_ptr_ }; }

private:
    storage_type* storage_ptr_;
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] pipe {
    pipe_in<ValueT, ErrorT, Atomic> in;
    pipe_out<ValueT, ErrorT, Atomic> out;
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
constexpr pipe<ValueT, ErrorT, Atomic> make_pipe() {
    auto* storage_ptr = new detail::pipe_storage<ValueT, ErrorT, Atomic>();
    return pipe<ValueT, ErrorT, Atomic>{
        .in{ storage_ptr },
        .out{ storage_ptr },
    };
}

} // namespace sl::exec
