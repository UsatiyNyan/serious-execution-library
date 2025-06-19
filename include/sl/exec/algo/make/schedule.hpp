//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/model/concept.hpp"

namespace sl::exec {
namespace detail {

template <typename F, typename ValueT, typename ErrorT>
struct [[nodiscard]] schedule_connection : task_node {
    constexpr schedule_connection(F&& functor, slot<ValueT, ErrorT>& slot, executor& executor)
        : functor_{ std::move(functor) }, slot_{ slot }, executor_{ executor } {}

    cancel_mixin& get_cancel_handle() & { return slot_; }

    void emit() && noexcept { executor_.schedule(this); }

    void execute() noexcept override {
        auto result = functor_();
        if (result.has_value()) {
            slot_.set_value(std::move(result).value());
        } else {
            slot_.set_error(std::move(result).error());
        }
    }
    void cancel() noexcept override { slot_.set_null(); }

private:
    F functor_;
    slot<ValueT, ErrorT>& slot_;
    executor& executor_;
};

template <typename F>
struct [[nodiscard]] schedule_signal {
    using result_type = std::invoke_result_t<F>;
    using value_type = typename result_type::value_type;
    using error_type = typename result_type::error_type;

public:
    constexpr schedule_signal(F functor, executor& executor) : functor_{ std::move(functor) }, executor_{ executor } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return schedule_connection<F, value_type, error_type>{
            /* .functor = */ std::move(functor_),
            /* .slot = */ slot,
            /* .executor = */ executor_,
        };
    }

    executor& get_executor() { return exec::inline_executor(); }

private:
    F functor_;
    executor& executor_;
};

} // namespace detail

template <typename FV>
constexpr SomeSignal auto schedule(executor& executor, FV&& functor) {
    using F = std::decay_t<FV>;
    return detail::schedule_signal<F>{
        /* .functor = */ std::forward<FV>(functor),
        /* .executor = */ executor,
    };
}

} // namespace sl::exec
