//
// Created by usatiynyan.
// usage:
//  - create via `share_box{std::move(signal)}` or `| share()`
//  - inherit internal result via `share_box::get_signal()`
// Signals created this way do not inherit executor, they continue inline.
// Is lazy until first subscription emit-s.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/syntax.hpp"

#include "sl/exec/algo/emit/subscribe.hpp"
#include "sl/exec/algo/sched/inline.hpp"

#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/intrusive/algorithm.hpp>
#include <sl/meta/lifetime/finalizer.hpp>
#include <sl/meta/monad/maybe.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, template <typename> typename Atomic>
struct [[nodiscard]] share_storage_base {
    using value_type = ValueT;
    using error_type = ErrorT;
    using result_type = meta::result<value_type, error_type>;

    enum share_state : std::uintptr_t {
        share_state_empty = std::numeric_limits<std::uintptr_t>::min(),
        share_state_result = std::numeric_limits<std::uintptr_t>::max(),
    };

    struct share_slot final : slot<value_type, error_type> {
        constexpr explicit share_slot(share_storage_base& self) : self_{ self } {}

        void set_value(value_type&& value) & override {
            self_.set_result(result_type{ tl::in_place, std::move(value) });
        }
        void set_error(error_type&& error) & override {
            self_.set_result(result_type{ tl::unexpect, std::move(error) });
        }
        void set_null() & override { self_.set_result(meta::null); }

    private:
        share_storage_base& self_;
    };

public: // type erasure
    virtual ~share_storage_base() = default;

protected:
    virtual void internal_connection_emit() & = 0;

public: // refcount
    constexpr explicit share_storage_base(std::uint32_t refcount) : refcount_{ refcount } {}

    std::uint32_t incref(std::uint32_t diff = 1) & {
        const std::uint32_t prev = refcount_.fetch_add(diff, std::memory_order::relaxed);
        return prev + diff;
    }

    std::uint32_t decref(std::uint32_t diff = 1) & {
        const std::uint32_t prev = refcount_.fetch_sub(diff, std::memory_order::relaxed);
        if (prev == diff) {
            delete this;
        }
        return prev - diff;
    }

    static void try_decref(share_storage_base* storage_ptr) {
        if (nullptr != storage_ptr) {
            storage_ptr->decref();
        }
    }

public: // fulfillment
    void emit(exec::slot_node<ValueT, ErrorT>& slot_node) & {
        std::uintptr_t state = share_state_empty;
        if (state_.compare_exchange_strong(
                state,
                reinterpret_cast<std::uintptr_t>(&slot_node),
                std::memory_order::release,
                std::memory_order::acquire
            )) {
            internal_connection_emit();
            return;
        }

        do {
            slot_node.intrusive_next = reinterpret_cast<exec::slot_node<value_type, error_type>*>(state);
        } while (state != share_state_result
                 && !state_.compare_exchange_weak(
                     state,
                     reinterpret_cast<std::uintptr_t>(&slot_node),
                     std::memory_order::release,
                     std::memory_order::acquire
                 ));

        if (state == share_state_result) {
            // exlicit copy, unfortunately
            fulfill_slot(slot_node, meta::maybe<result_type>{ maybe_result_ });
            decref();
        }
    }

private:
    void set_result(meta::maybe<result_type> maybe_result) & {
        maybe_result_ = std::move(maybe_result);

        std::uintptr_t state = state_.exchange(share_state_result, std::memory_order::acq_rel);
        const bool state_is_empty = state == share_state_empty;
        ASSUME(!state_is_empty, "set_result before emit, since this is lazy impl");
        if (state_is_empty) {
            return;
        }
        DEBUG_ASSERT(state != share_state_result);

        auto* slot_list = reinterpret_cast<exec::slot_node<value_type, error_type>*>(state);

        // if any exceptions are thrown, probably everything breaks here
        std::uint32_t slot_list_count = 0;

        meta::intrusive_forward_list_node_for_each(
            slot_list,
            [&slot_list_count,
             &maybe_result_ref = maybe_result_](exec::slot_node<value_type, error_type>* slot_node_ptr) {
                // exlicit copy, unfortunately
                fulfill_slot(*slot_node_ptr, meta::maybe<result_type>{ maybe_result_ref });
                ++slot_list_count;
            }
        );

        decref(slot_list_count);
    }

private:
    meta::maybe<result_type> maybe_result_{};
    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> refcount_{ 0 };
    alignas(hardware_destructive_interference_size) Atomic<std::uintptr_t> state_{ share_state_empty };
};

template <
    SomeSignal SignalT,
    template <typename>
    typename Atomic,
    typename ValueT = typename SignalT::value_type,
    typename ErrorT = typename SignalT::error_type>
struct [[nodiscard]] share_storage final : share_storage_base<ValueT, ErrorT, Atomic> {
    using base_type = share_storage_base<ValueT, ErrorT, Atomic>;
    using slot_type = typename base_type::share_slot;

public:
    constexpr share_storage(SignalT&& signal, std::uint32_t refcount)
        : base_type{ refcount }, connection_{ std::move(signal), slot_type{ *this } } {}

protected:
    void internal_connection_emit() & override { std::move(connection_).emit(); }

private:
    subscribe_connection<SignalT, slot_type> connection_;
};


template <typename ValueT, typename ErrorT, template <typename> typename Atomic>
struct [[nodiscard]] share_connection : meta::immovable {
    constexpr share_connection(
        detail::share_storage_base<ValueT, ErrorT, Atomic>* storage_ptr,
        slot<ValueT, ErrorT>& slot
    )
        : slot_node_{ slot }, storage_ptr_{ storage_ptr } {}
    ~share_connection() { detail::share_storage_base<ValueT, ErrorT, Atomic>::try_decref(storage_ptr_); }

    void emit() && {
        auto* storage_ptr = std::exchange(storage_ptr_, nullptr);
        DEBUG_ASSERT(nullptr != storage_ptr);
        storage_ptr->emit(slot_node_);
    }

private:
    exec::slot_node<ValueT, ErrorT> slot_node_;
    detail::share_storage_base<ValueT, ErrorT, Atomic>* storage_ptr_;
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic>
struct [[nodiscard]] share_signal : meta::finalizer<share_signal<ValueT, ErrorT, Atomic>> {
    using value_type = ValueT;
    using error_type = ErrorT;

public:
    constexpr explicit share_signal(detail::share_storage_base<value_type, error_type, Atomic>* storage_ptr)
        : meta::finalizer<share_signal>{ [](share_signal& self) {
              detail::share_storage_base<value_type, error_type, Atomic>::try_decref(self.storage_ptr_);
          } },
          storage_ptr_{ storage_ptr } {
        [[maybe_unused]] const std::uint32_t refcount = storage_ptr_->incref();
        DEBUG_ASSERT(refcount > 1u);
    }

    constexpr Connection auto subscribe(slot<value_type, error_type>& slot) && {
        auto* storage_ptr = std::exchange(storage_ptr_, nullptr);
        DEBUG_ASSERT(nullptr != storage_ptr);
        return share_connection{
            /* .storage_ptr = */ storage_ptr,
            /* .slot = */ slot,
        };
    }

    executor& get_executor() & { return exec::inline_executor(); }

private:
    detail::share_storage_base<value_type, error_type, Atomic>* storage_ptr_;
};

} // namespace detail

template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] share_box : meta::finalizer<share_box<ValueT, ErrorT, Atomic>> {

    template <SomeSignal SignalT>
    constexpr explicit share_box(SignalT&& signal)
        : meta::finalizer<share_box>{ [](share_box& self) { self.storage_ptr_->decref(); } },
          storage_ptr_{ new detail::share_storage<SignalT, Atomic>{
              /* .signal = */ std::move(signal),
              /* .refcount = */ 1u,
          } } {}

    constexpr Signal<ValueT, ErrorT> auto get_signal() & {
        return detail::share_signal<ValueT, ErrorT, Atomic>{ storage_ptr_ };
    }

private:
    detail::share_storage_base<ValueT, ErrorT, Atomic>* storage_ptr_;
};

namespace detail {

template <template <typename> typename Atomic>
struct [[nodiscard]] share_emit {
    template <SomeSignal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return exec::share_box<typename SignalT::value_type, typename SignalT::error_type, Atomic>{ std::move(signal) };
    }
};

} // namespace detail

template <template <typename> typename Atomic = detail::atomic>
constexpr auto share() {
    return detail::share_emit<Atomic>{};
}

} // namespace sl::exec
