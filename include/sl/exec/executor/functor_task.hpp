//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/executor/schedule.hpp"
#include "sl/exec/executor/task.hpp"

#include <type_traits>

namespace sl::exec {

template <typename F>
concept FunctorTaskNodeRequirement = std::is_nothrow_invocable_r_v<void, F>;

template <FunctorTaskNodeRequirement F>
class functor_task_node final : public task_node {
public:
    template <typename FV>
    explicit functor_task_node(FV&& f) : f_{ std::forward<FV>(f) } {}

    void execute() noexcept override {
        f_();
        delete this;
    }

private:
    F f_;
};

namespace detail {

template <FunctorTaskNodeRequirement F>
struct schedule<F> {
    template <typename FV>
    static void impl(executor& executor, FV&& f) {
        auto* node = new (std::nothrow) functor_task_node<std::decay_t<FV>>{ std::forward<FV>(f) };
        executor.schedule(node);
    }
};

} // namespace detail
} // namespace sl::exec
