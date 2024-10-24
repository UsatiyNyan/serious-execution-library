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
struct [[nodiscard]] map_error_slot : task_node {
    map_error_slot(SlotT&& slot, F&& functor, executor& executor)
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
        auto error = functor_(std::move(maybe_error_).value());
        slot_.set_error(std::move(error));
    }
    void cancel() noexcept override { slot_.cancel(); }

private:
    SlotT slot_;
    F functor_;
    executor& executor_;
    ::tl::optional<ErrorT> maybe_error_;
};

template <Signal SignalT, typename F>
struct [[nodiscard]] map_error_signal {
    using value_type = typename SignalT::value_type;
    using error_type = std::invoke_result_t<F, typename SignalT::error_type>;

    SignalT signal;
    F functor;

    template <Slot<value_type, error_type> SlotT>
    Connection auto subscribe(SlotT&& slot) && {
        return std::move(signal).subscribe(map_error_slot<value_type, typename SignalT::error_type, SlotT, F>{
            /* .slot = */ std::move(slot),
            /* .functor = */ std::move(functor),
            /* .executor = */ get_executor(),
        });
    }

    executor& get_executor() { return signal.get_executor(); }
};

template <typename F>
struct [[nodiscard]] map_error {
    F functor;

    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return map_error_signal<SignalT, F>{
            .signal = std::move(signal),
            .functor = std::move(functor),
        };
    }
};

} // namespace detail

template <typename FV>
constexpr auto map_error(FV&& functor) {
    using F = std::decay_t<FV>;
    return detail::map_error<F>{ .functor = std::forward<FV>(functor) };
}

} // namespace sl::exec
