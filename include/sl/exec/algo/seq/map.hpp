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
struct [[nodiscard]] map_slot : task_node {
    map_slot(SlotT&& slot, F&& functor, executor& executor)
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
        auto value = functor_(std::move(maybe_value_).value());
        slot_.set_value(std::move(value));
    }
    void cancel() noexcept override { slot_.cancel(); }

private:
    SlotT slot_;
    F functor_;
    executor& executor_;
    ::tl::optional<ValueT> maybe_value_;
};

template <Signal SignalT, typename F>
struct [[nodiscard]] map_signal {
    using value_type = std::invoke_result_t<F, typename SignalT::value_type>;
    using error_type = typename SignalT::error_type;

    SignalT signal;
    F functor;

    template <Slot<value_type, error_type> SlotT>
    Connection auto subscribe(SlotT&& slot) && {
        return std::move(signal).subscribe(map_slot<typename SignalT::value_type, error_type, SlotT, F>{
            /* .slot = */ std::move(slot),
            /* .functor = */ std::move(functor),
            /* .executor = */ get_executor(),
        });
    }

    executor& get_executor() { return signal.get_executor(); }
};

template <typename F>
struct [[nodiscard]] map {
    F functor;

    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return map_signal<SignalT, F>{
            .signal = std::move(signal),
            .functor = std::move(functor),
        };
    }
};

} // namespace detail

template <typename FV>
constexpr auto map(FV&& functor) {
    using F = std::decay_t<FV>;
    return detail::map<F>{ .functor = std::forward<FV>(functor) };
}

} // namespace sl::exec
