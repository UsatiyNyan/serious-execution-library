//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"

namespace sl::exec {
namespace detail {

template <SomeSignal SignalT>
struct query_executor_signal final {
    template <typename U>
    using with_executor = std::pair<U, executor&>;

    using value_type = with_executor<typename SignalT::value_type>;
    using error_type = with_executor<typename SignalT::error_type>;

    struct query_executor_slot final : slot<typename SignalT::value_type, typename SignalT::error_type> {
        constexpr query_executor_slot(executor& executor, slot<value_type, error_type>& slot)
            : executor_{ executor }, slot_{ slot } {}

        void set_value(typename SignalT::value_type&& value) & override {
            slot_.set_value(value_type{ std::move(value), executor_ });
        }
        void set_error(typename SignalT::error_type&& error) & override {
            slot_.set_error(error_type{ std::move(error), executor_ });
        }
        void set_null() & override { slot_.set_null(); }

    private:
        executor& executor_;
        slot<value_type, error_type>& slot_;
    };

public:
    constexpr explicit query_executor_signal(SignalT&& signal) : signal_{ std::move(signal) } {}

    subscribe_connection<SignalT, query_executor_slot> subscribe(slot<value_type, error_type>& slot) && {
        executor& executor = signal_.get_executor();
        return subscribe_connection<SignalT, query_executor_slot>{
            std::move(signal_),
            [&] { return query_executor_slot{ executor, slot }; },
        };
    }

    executor& get_executor() { return signal_.get_executor(); }

private:
    SignalT signal_;
};

struct [[nodiscard]] query_executor final {
    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && {
        return query_executor_signal<SignalT>{ /* .signal = */ std::move(signal) };
    }
};

} // namespace detail

constexpr auto query_executor() { return detail::query_executor{}; }

} // namespace sl::exec
