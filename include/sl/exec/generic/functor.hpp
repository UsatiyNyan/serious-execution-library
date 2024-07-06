//
// Created by usatiynyan.
// Since we are already allocating tasks on heap its fine to have virtual interfaces.
//

#pragma once

#include "sl/exec/generic/executor.hpp"
#include "sl/exec/generic/task.hpp"

#include <libassert/assert.hpp>
#include <type_traits>

namespace sl::exec {

template <typename F>
    requires(std::is_nothrow_invocable_r_v<void, F, generic_executor&>)
class functor_task_node : public generic_task_node {
    template <typename FV>
    explicit functor_task_node(FV&& f) : f_{ std::forward<FV>(f) } {}

public:
    template <typename FV>
    static functor_task_node* allocate(FV&& f) noexcept {
        return new (std::nothrow) functor_task_node{ std::forward<FV>(f) };
    }

    generic_cleanup execute(generic_executor& executor) noexcept override {
        f_(executor);
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

template <typename FV>
bool schedule(generic_executor& executor, FV&& f) {
    using functor_type = functor_task_node<std::decay_t<FV>>;
    auto* node = functor_type::allocate(std::forward<FV>(f));
    if (ASSUME_VAL(node != nullptr)) {
        return executor.schedule(node);
    }
    return false;
}

} // namespace sl::exec
