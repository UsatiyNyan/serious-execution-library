//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

#include <sl/meta/monad/maybe.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename InputErrorT, typename ErrorT, typename F>
struct [[nodiscard]] map_error_slot : slot<ValueT, InputErrorT> {
    struct map_error_task : task_node {
        explicit map_error_task(map_error_slot& self) : self_{ self } {}

        void execute() noexcept override {
            if (!ASSUME_VAL(self_.maybe_error_.has_value())) {
                return;
            }
            auto error = self_.functor_(std::move(self_.maybe_error_).value());
            self_.slot_.set_error(std::move(error));
        }
        void cancel() noexcept override { self_.set_null(); };

    private:
        map_error_slot& self_;
    };

    map_error_slot(F&& functor, slot<ValueT, ErrorT>& slot, executor& executor)
        : functor_{ std::move(functor) }, slot_{ slot }, executor_{ executor } {}

    void setup_cancellation() & override { slot_.intrusive_next = this; }

    void set_value(ValueT&& value) & override { slot_.set_value(std::move(value)); }
    void set_error(InputErrorT&& error) & override {
        maybe_error_.emplace(std::move(error));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(&task);
    }
    void set_null() & override { slot_.set_null(); }

private:
    F functor_;
    meta::maybe<InputErrorT> maybe_error_{};
    meta::maybe<map_error_task> maybe_task_{};
    slot<ValueT, ErrorT>& slot_;
    executor& executor_;
};

template <SomeSignal SignalT, typename F>
struct [[nodiscard]] map_error_signal {
    using value_type = typename SignalT::value_type;
    using error_type = std::invoke_result_t<F, typename SignalT::error_type>;

    SignalT signal;
    F functor;

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        executor& executor = signal.get_executor();
        return subscribe_connection{
            /* .signal = */ std::move(signal),
            /* .slot = */
            map_error_slot<value_type, typename SignalT::error_type, error_type, F>{
                /* .functor = */ std::move(functor),
                /* .slot = */ slot,
                /* .executor = */ executor,
            },
        };
    }

    executor& get_executor() { return signal.get_executor(); }
};

template <typename F>
struct [[nodiscard]] map_error {
    F functor;

    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && {
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
