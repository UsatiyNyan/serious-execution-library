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

template <typename InputValueT, typename ValueT, typename ErrorT, typename F>
struct [[nodiscard]] and_then_slot : slot<InputValueT, ErrorT> {
    struct and_then_task : task_node {
        explicit and_then_task(and_then_slot& self) : self_{ self } {}

        void execute() noexcept override {
            if (!ASSUME_VAL(self_.maybe_value_.has_value())) {
                return;
            }
            auto result = self_.functor_(std::move(self_.maybe_value_).value());
            if (result.has_value()) {
                self_.slot_.set_value(std::move(result).value());
            } else {
                self_.slot_.set_error(std::move(result).error());
            }
        }
        void cancel() noexcept override { self_.cancel(); };

    private:
        and_then_slot& self_;
    };

    and_then_slot(F&& functor, slot<ValueT, ErrorT>& slot, executor& executor)
        : functor_{ std::move(functor) }, slot_{ slot }, executor_{ executor } {}

    void set_value(InputValueT&& value) & override {
        maybe_value_.emplace(std::move(value));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(&task);
    }
    void set_error(ErrorT&& error) & override { slot_.set_error(std::move(error)); }
    void cancel() & override { slot_.cancel(); }

private:
    F functor_;
    meta::maybe<InputValueT> maybe_value_{};
    meta::maybe<and_then_task> maybe_task_{};
    slot<ValueT, ErrorT>& slot_;
    executor& executor_;
};

template <Signal SignalT, typename F, typename ResultT = std::invoke_result_t<F, typename SignalT::value_type>>
    requires std::same_as<typename SignalT::error_type, typename ResultT::error_type>
struct [[nodiscard]] and_then_signal {
    using value_type = typename ResultT::value_type;
    using error_type = typename ResultT::error_type;

    SignalT signal;
    F functor;

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return subscribe_connection{
            /* .signal = */ std::move(signal),
            /* .slot = */
            and_then_slot<typename SignalT::value_type, value_type, error_type, F>{
                /* .functor = */ std::move(functor),
                /* .slot = */ slot,
                /* .executor = */ get_executor(),
            },
        };
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
