//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

namespace sl::exec {
namespace detail {

template <Signal SignalT>
struct [[nodiscard]] on_signal {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

public:
    on_signal(SignalT&& signal, executor& executor) : signal_{ std::move(signal) }, executor_{ executor } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && { return std::move(signal_).subscribe(slot); }

    executor& get_executor() { return executor_; }

private:
    SignalT signal_;
    executor& executor_;
};

struct [[nodiscard]] on {
    executor& executor;

    template <Signal SignalT>
    constexpr Signal auto operator()(SignalT&& signal) && {
        return on_signal<SignalT>{
            /* .signal = */ std::move(signal),
            /* .executor = */ executor,
        };
    }
};

} // namespace detail


constexpr auto on(executor& executor) {
    return detail::on{
        .executor = executor,
    };
}

} // namespace sl::exec
