//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/connection.hpp"

namespace sl::exec {
namespace detail {

template <typename U>
using with_executor = std::pair<U, executor&>;

template <SomeSignal SignalT, typename SlotT>
struct [[nodiscard]] query_executor_connection final {
    using input_value_type = typename SignalT::value_type;
    using input_error_type = typename SignalT::error_type;
    using value_type = with_executor<input_value_type>;
    using error_type = with_executor<input_error_type>;

    struct query_executor_slot final {
        executor& ex;
        SlotT slot;

        constexpr void set_value(input_value_type&& value) && noexcept {
            std::move(slot).set_value(value_type{ std::move(value), ex });
        }
        constexpr void set_error(input_error_type&& error) && noexcept {
            std::move(slot).set_error(error_type{ std::move(error), ex });
        }
        constexpr void set_null() && noexcept { std::move(slot).set_null(); }
    };

    struct query_executor_slot_ctor final {
        executor& ex;
        SlotT slot;

        constexpr auto operator()() && noexcept { return query_executor_slot{ ex, std::move(slot) }; }
    };

    ConnectionFor<SignalT, query_executor_slot_ctor> connection;

    constexpr CancelHandle auto emit() && noexcept { return std::move(connection).emit(); }
};

template <SomeSignal SignalT>
struct [[nodiscard]] query_executor_signal final {
    using value_type = with_executor<typename SignalT::value_type>;
    using error_type = with_executor<typename SignalT::error_type>;

    SignalT signal;

public:
    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT slot_ctor) && noexcept {
        using SlotT = SlotFrom<SlotCtorT>;
        using ConnectionT = query_executor_connection<SignalT, SlotT>;
        using SlotCtorForSignal = typename ConnectionT::query_executor_slot_ctor;
        executor& ex = signal.get_executor();
        return ConnectionT{
            .connection = std::move(signal).subscribe(SlotCtorForSignal{ ex, std::move(slot_ctor)() }),
        };
    }

    constexpr executor& get_executor() noexcept { return signal.get_executor(); }
};

struct [[nodiscard]] query_executor final {
    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && noexcept {
        return query_executor_signal<SignalT>{ .signal = std::move(signal) };
    }
};

} // namespace detail

constexpr auto query_executor() noexcept { return detail::query_executor{}; }

} // namespace sl::exec
