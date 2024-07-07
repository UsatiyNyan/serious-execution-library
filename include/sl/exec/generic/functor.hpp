//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/generic/executor.hpp"
#include "sl/exec/generic/task.hpp"

#include <libassert/assert.hpp>
#include <type_traits>

namespace sl::exec {

template <typename F>
concept FunctorTaskNodeRequirement = std::is_nothrow_invocable_r_v<void, F>;

template <FunctorTaskNodeRequirement F>
class functor_task_node : public generic_task_node {
    template <typename FV>
    explicit functor_task_node(FV&& f) : f_{ std::forward<FV>(f) } {}

public:
    template <typename FV>
    static functor_task_node* allocate(FV&& f) noexcept {
        return new (std::nothrow) functor_task_node{ std::forward<FV>(f) };
    }

    generic_cleanup execute() noexcept override {
        f_();
        return generic_cleanup{ [this] { delete this; } };
    }
    generic_cleanup cancel() noexcept override {
        return generic_cleanup{ [this] { delete this; } };
    }

private:
    F f_;
};

template <typename FV>
functor_task_node(FV&&) -> functor_task_node<std::decay_t<FV>>;

namespace detail {

template <FunctorTaskNodeRequirement F>
struct schedule<F> {
    template <typename FV>
    static bool impl(generic_executor& executor, FV&& f) {
        auto* node = functor_task_node<F>::allocate(std::forward<FV>(f));
        if (ASSUME_VAL(node != nullptr)) {
            return executor.schedule(node);
        }
        return false;
    }
};

} // namespace detail
} // namespace sl::exec
