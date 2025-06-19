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
struct [[nodiscard]] or_else_slot : slot<ValueT, InputErrorT> {
    struct or_else_task : task_node {
        explicit or_else_task(or_else_slot& self) : self_{ self } {}

        void execute() noexcept override {
            if (!ASSUME_VAL(self_.maybe_error_.has_value())) {
                return;
            }
            auto result = self_.functor_(std::move(self_.maybe_error_).value());
            if (result.has_value()) {
                self_.slot_.set_value(std::move(result).value());
            } else {
                self_.slot_.set_error(std::move(result).error());
            }
        }
        void cancel() noexcept override { self_.set_null(); };

    private:
        or_else_slot& self_;
    };

    or_else_slot(F&& functor, slot<ValueT, ErrorT>& slot, executor& executor)
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
    meta::maybe<or_else_task> maybe_task_{};
    slot<ValueT, ErrorT>& slot_;
    executor& executor_;
};

template <SomeSignal SignalT, typename F, typename ResultT = std::invoke_result_t<F, typename SignalT::error_type>>
    requires std::same_as<typename SignalT::value_type, typename ResultT::value_type>
struct [[nodiscard]] or_else_signal {
    using value_type = typename ResultT::value_type;
    using error_type = typename ResultT::error_type;

    SignalT signal;
    F functor;

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        executor& executor = signal.get_executor();
        return subscribe_connection{
            /* .signal = */ std::move(signal),
            /* .slot = */
            or_else_slot<value_type, typename SignalT::error_type, error_type, F>{
                /* .functor = */ std::move(functor),
                /* .slot = */ slot,
                /* .executor = */ executor,
            },
        };
    }

    executor& get_executor() { return signal.get_executor(); }
};

template <typename F>
struct [[nodiscard]] or_else {
    F functor;

    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && {
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
