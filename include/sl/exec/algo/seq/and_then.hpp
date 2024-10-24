//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, typename SlotT, typename F>
struct [[nodiscard]] and_then_slot : task_node {
    and_then_slot(SlotT&& slot, F&& functor, executor& executor)
        : slot_{ std::move(slot) }, functor_{ std::move(functor) }, executor_{ executor } {}

    void set_value(ValueT&& value) & {
        maybe_value_.emplace(std::move(value));
        executor_.schedule(this);
    }
    void set_error(ErrorT&& error) & { slot_.set_error(std::move(error)); }

    void execute() noexcept override {
        if (!ASSUME_VAL(maybe_value_.has_value())) {
            return;
        }
        auto result = functor_(std::move(maybe_value_).value());
        if (result.has_value()) {
            slot_.set_value(std::move(result).value());
        } else {
            slot_.set_error(std::move(result).error());
        }
    }
    void cancel() noexcept override { slot_.cancel(); }

private:
    SlotT slot_;
    F functor_;
    executor& executor_;
    ::tl::optional<ValueT> maybe_value_;
};

template <Signal SignalT, typename F, typename ResultT = std::invoke_result_t<F, typename SignalT::value_type>>
    requires std::same_as<typename SignalT::error_type, typename ResultT::error_type>
struct [[nodiscard]] and_then_signal {
    using value_type = typename ResultT::value_type;
    using error_type = typename ResultT::error_type;

    SignalT signal;
    F functor;

    template <Slot<value_type, error_type> SlotT>
    Connection auto subscribe(SlotT&& slot) && {
        return std::move(signal).subscribe(and_then_slot<typename SignalT::value_type, error_type, SlotT, F>{
            /* .slot = */ std::move(slot),
            /* .functor = */ std::move(functor),
            /* .executor = */ get_executor(),
        });
    }

    executor& get_executor() { return signal.get_executor(); }
};

template <typename F>
struct [[nodiscard]] and_then {
    F functor;

    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return and_then_signal<SignalT, F>{
            .signal = std::move(signal),
            .functor = std::move(functor),
        };
    }
};

} // namespace detail

template <typename FV>
constexpr auto and_then(FV&& functor) {
    using F = std::decay_t<FV>;
    return detail::and_then<F>{ .functor = std::forward<FV>(functor) };
}

} // namespace sl::exec
