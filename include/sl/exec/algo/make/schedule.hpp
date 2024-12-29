//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

namespace sl::exec {
namespace detail {

template <typename F, typename ValueT, typename ErrorT>
struct [[nodiscard]] schedule_connection : task_node {
    schedule_connection(F&& functor, slot<ValueT, ErrorT>& slot, executor& executor)
        : functor_{ std::move(functor) }, slot_{ slot }, executor_{ executor } {}

    void emit() & noexcept { executor_.schedule(this); }

    void execute() noexcept override {
        auto result = functor_();
        if (result.has_value()) {
            slot_.set_value(std::move(result).value());
        } else {
            slot_.set_error(std::move(result).error());
        }
    }
    void cancel() noexcept override { slot_.cancel(); }

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

    F functor;
    executor& executor;

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return schedule_connection<F, value_type, error_type>{
            /* .functor = */ std::move(functor),
            /* .slot = */ slot,
            /* .executor = */ executor,
        };
    }

    auto& get_executor() { return executor; }
};

} // namespace detail

template <typename FV>
constexpr Signal auto schedule(executor& executor, FV&& functor) {
    using F = std::decay_t<FV>;
    return detail::schedule_signal<F>{
        .functor = std::forward<FV>(functor),
        .executor = executor,
    };
}

constexpr Signal auto schedule(executor& executor) {
    return schedule(executor, [] { return meta::result<meta::unit, meta::undefined>{}; });
}

} // namespace sl::exec
