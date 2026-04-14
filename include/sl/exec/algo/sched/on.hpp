//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/result.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

namespace sl::exec {
namespace detail {

template <SomeSignal SignalT>
struct [[nodiscard]] continue_on_signal final {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

    SignalT signal;
    executor& ex;

public:
    template <SlotCtorFor<SignalT> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        return std::move(signal).subscribe(std::move(slot_ctor));
    }

    executor& get_executor() noexcept { return ex; }
};

struct [[nodiscard]] continue_on final {
    executor& ex;

    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && noexcept {
        return continue_on_signal<SignalT>{
            .signal = std::move(signal),
            .ex = ex,
        };
    }
};

} // namespace detail


constexpr auto continue_on(executor& an_executor) noexcept { return detail::continue_on{ .ex = an_executor }; }

constexpr Signal<meta::unit, meta::undefined> auto start_on(executor& an_executor) noexcept {
    return continue_on(an_executor)( //
        as_signal(meta::ok(meta::unit{}))
    );
}

} // namespace sl::exec
