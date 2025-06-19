//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/detail/atomic.hpp"

#include "sl/exec/model/concept.hpp"

#include "sl/exec/algo/emit/subscribe.hpp"

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, template <typename> typename Atomic>
struct cancellable_slot : slot<ValueT, ErrorT> {
    enum cancelled_state : std::uint32_t {
        cancelled_state_none,
        cancelled_state_cancelled,
        cancelled_state_fulfilled,
    };

public:
    explicit cancellable_slot(slot<ValueT, ErrorT>& slot) : slot_{ slot } {}

    void setup_cancellation() & override { slot_.intrusive_next = this; }

    void set_value(ValueT&& value) & override {
        if (try_fulfill()) {
            slot_.set_value(std::move(value));
        }
    }
    void set_error(ErrorT&& error) & override {
        if (try_fulfill()) {
            slot_.set_error(std::move(error));
        }
    }
    void set_null() & override {
        if (try_fulfill()) {
            slot_.set_null();
        }
    }

    bool try_cancel() & override {
        auto expected = cancelled_state_none;
        return cancelled_.compare_exchange_strong(
            expected, cancelled_state_cancelled, std::memory_order::relaxed, std::memory_order::relaxed
        );
    }

private:
    bool try_fulfill() & {
        auto expected = cancelled_state_none;
        return cancelled_.compare_exchange_strong(
            expected, cancelled_state_fulfilled, std::memory_order::relaxed, std::memory_order::relaxed
        );
    }

private:
    slot<ValueT, ErrorT>& slot_;
    Atomic<cancelled_state> cancelled_{ cancelled_state_none };
};

template <SomeSignal SignalT, template <typename> typename Atomic>
struct [[nodiscard]] cancellable_signal {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

public:
    constexpr explicit cancellable_signal(SignalT&& signal) : signal_{ std::move(signal) } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return subscribe_connection<SignalT, cancellable_slot<value_type, error_type, Atomic>>{
            std::move(signal_),
            [&slot] { return cancellable_slot<value_type, error_type, Atomic>{ slot }; },
        };
    }

    executor& get_executor() { return signal_.get_executor(); }

private:
    SignalT signal_;
};

template <template <typename> typename Atomic>
struct [[nodiscard]] cancellable {

    template <SomeSignal SignalT>
    constexpr Signal<typename SignalT::value_type, typename SignalT::error_type> auto operator()(SignalT&& signal) && {
        return cancellable_signal<SignalT, Atomic>{
            /* .signal = */ std::move(signal),
        };
    }
};

} // namespace detail

template <template <typename> typename Atomic = detail::atomic>
constexpr auto cancellable() {
    return detail::cancellable<Atomic>{};
}

} // namespace sl::exec
