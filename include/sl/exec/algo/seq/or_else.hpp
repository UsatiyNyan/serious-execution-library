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
struct [[nodiscard]] or_else_slot : task_node {
    or_else_slot(SlotT&& slot, F&& functor, executor& executor)
        : slot_{ std::move(slot) }, functor_{ std::move(functor) }, executor_{ executor } {}

    void set_value(ValueT&& value) & { slot_.set_value(std::move(value)); }
    void set_error(ErrorT&& error) & {
        maybe_error_.emplace(std::move(error));
        executor_.schedule(this);
    }

    void execute() noexcept override {
        if (!ASSUME_VAL(maybe_error_.has_value())) {
            return;
        }
        auto result = functor_(std::move(maybe_error_).value());
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
    ::tl::optional<ErrorT> maybe_error_;
};

template <Signal SignalT, typename F, typename ResultT = std::invoke_result_t<F, typename SignalT::error_type>>
    requires std::same_as<typename SignalT::value_type, typename ResultT::value_type>
struct [[nodiscard]] or_else_signal {
    using value_type = typename ResultT::value_type;
    using error_type = typename ResultT::error_type;

    SignalT signal;
    F functor;

    template <Slot<value_type, error_type> SlotT>
    Connection auto subscribe(SlotT&& slot) && {
        return std::move(signal).subscribe(or_else_slot<value_type, typename SignalT::error_type, SlotT, F>{
            /* .slot = */ std::move(slot),
            /* .functor = */ std::move(functor),
            /* .executor = */ get_executor(),
        });
    }

    executor& get_executor() { return signal.get_executor(); }
};

template <typename F>
struct [[nodiscard]] or_else {
    F functor;

    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return or_else_signal<SignalT, F>{
            .signal = std::move(signal),
            .functor = std::move(functor),
        };
    }
};

} // namespace detail

template <typename FV>
constexpr auto or_else(FV&& functor) {
    using F = std::decay_t<FV>;
    return detail::or_else<F>{ .functor = std::forward<FV>(functor) };
}

} // namespace sl::exec
