//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

namespace sl::exec {
namespace detail {

template <SomeSignal SignalT>
struct [[nodiscard]] continue_on_signal {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

public:
    constexpr continue_on_signal(SignalT&& signal, executor& executor)
        : signal_{ std::move(signal) }, executor_{ executor } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && { return std::move(signal_).subscribe(slot); }

    executor& get_executor() { return executor_; }

private:
    SignalT signal_;
    executor& executor_;
};

struct [[nodiscard]] continue_on {
    constexpr explicit continue_on(executor& executor) : executor_{ executor } {}

    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && {
        return continue_on_signal<SignalT>{
            /* .signal = */ std::move(signal),
            /* .executor = */ executor_,
        };
    }

private:
    executor& executor_;
};

} // namespace detail


constexpr auto continue_on(executor& executor) {
    return detail::continue_on{
        /* .executor = */ executor,
    };
}

} // namespace sl::exec
