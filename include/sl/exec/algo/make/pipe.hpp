//
// Created by usatiynyan.
//
// `pipe` is an SPSC(Single Producer Single Consumer) channel
// Signals in.send(...) and out.receive() can be created multiple times
// subscriptions will still be one-shot
// REQUIREMENT: have to wait for `send`/`receive` signals to finish before destroying `in`/`out`

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

    struct pipe_state : cancel_mixin {
        enum class tag { in, out };

        virtual ~pipe_state() = default;
        virtual tag get_tag() const& = 0;

        bool is_in() const& { return get_tag() == tag::in; }
        bool is_out() const& { return get_tag() == tag::out; }
    };

    struct [[nodiscard]] in_type final : pipe_state {
        pipe_state::tag get_tag() const& override { return pipe_state::tag::in; }

    public:
        in_type(
            meta::maybe<result_type> maybe_result,
            pipe_storage& storage,
            slot<meta::unit, meta::undefined>& in_slot
        )
            : maybe_result_{ std::move(maybe_result) }, storage_{ storage }, in_slot_{ in_slot } {
            in_slot_.intrusive_next = this;
        }

        // Connection
        cancel_mixin& get_cancel_handle() & {
            ASSERT(in_slot_.intrusive_next == this);
            return in_slot_;
        }
        void emit() && { storage_.emit_in(*this); }

        // cancel_mixin
        bool try_cancel() & override { return storage_.unemit_in(*this); }

        void fulfill(slot<ValueT, ErrorT>& out_slot) noexcept {
            fulfill_slot(out_slot, std::move(maybe_result_));
            in_slot_.set_value(meta::unit{});
        }

    private:
        meta::maybe<result_type> maybe_result_;
        pipe_storage& storage_;
        slot<meta::unit, meta::undefined>& in_slot_;
    };

    struct [[nodiscard]] out_type final : pipe_state {
        pipe_state::tag get_tag() const& override { return pipe_state::tag::out; }

    public:
        out_type(pipe_storage& storage, slot<ValueT, ErrorT>& slot) : storage_{ storage }, out_slot_{ slot } {
            out_slot_.intrusive_next = this;
        }

        // Connection
        cancel_mixin& get_cancel_handle() & {
            ASSERT(out_slot_.intrusive_next == this);
            return out_slot_;
        }
        void emit() && { storage_.subscribe_out(*this); }

        // cancel_mixin
        bool try_cancel() & override { return storage_.unsubscribe_out(*this); }

        slot<ValueT, ErrorT>& get_slot() const& { return out_slot_; }

    private:
        pipe_storage& storage_;
        slot<ValueT, ErrorT>& out_slot_;
    };

public:
    void emit_in(in_type& in) & {
        while (true) {
            pipe_state* state = nullptr;
            if (state_.compare_exchange_strong(state, &in, std::memory_order::release, std::memory_order::acquire)) {
                break;
            }
            DEBUG_ASSERT(state->is_out(), "only single producer");

            if (state_.compare_exchange_strong(
                    state, nullptr, std::memory_order::relaxed, std::memory_order::relaxed
                )) {
                out_type& out = *static_cast<out_type*>(state);
                in.fulfill(out.get_slot());
                break;
            }

            // unsubscribed on the other end
            DEBUG_ASSERT(state == nullptr);
        }
    }

    void subscribe_out(out_type& out) & {
        while (true) {
            pipe_state* state = nullptr;
            if (state_.compare_exchange_strong(state, &out, std::memory_order::release, std::memory_order::acquire)) {
                break;
            }
            DEBUG_ASSERT(state->is_in(), "only single consumer");

            if (state_.compare_exchange_strong(
                    state, nullptr, std::memory_order::relaxed, std::memory_order::relaxed
                )) {
                in_type& in = *static_cast<in_type*>(state);
                in.fulfill(out.get_slot());
                break;
            }

            // unemitted on the other end
            DEBUG_ASSERT(state == nullptr);
        }
    }

    bool unemit_in(in_type& in) & {
        pipe_state* expected = &in;
        return state_.compare_exchange_strong(
            expected, nullptr, std::memory_order::relaxed, std::memory_order::relaxed
        );
    }

    bool unsubscribe_out(out_type& out) & {
        pipe_state* expected = &out;
        return state_.compare_exchange_strong(
            expected, nullptr, std::memory_order::relaxed, std::memory_order::relaxed
        );
    }

public: // refcount
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
    using connection_type = storage_type::in_type;

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

template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] pipe_out : meta::finalizer<pipe_out<ValueT, ErrorT, Atomic>> {
    using storage_type = detail::pipe_storage<ValueT, ErrorT, Atomic>;
    using connection_type = storage_type::out_type;

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
