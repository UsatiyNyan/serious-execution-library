//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/generic/task.hpp"

#include <libassert/assert.hpp>
#include <type_traits>

namespace sl::exec {

template <typename F>
concept FunctorTaskNodeRequirement = std::is_nothrow_invocable_r_v<void, F>;

template <FunctorTaskNodeRequirement F>
class functor_task_node : public generic_task_node {
public:
    template <typename FV>
    explicit functor_task_node(FV&& f) : f_{ std::forward<FV>(f) } {}

    generic_cleanup execute() noexcept override {
        f_();
        return generic_cleanup{ [this] {
            ASSUME(this != nullptr);
            delete this;
        } };
    }
    generic_cleanup cancel() noexcept override {
        return generic_cleanup{ [this] {
            ASSUME(this != nullptr);
            delete this;
        } };
    }

private:
    F f_;
};

template <typename FV>
auto allocate_functor_task_node(FV&& f) {
    return new (std::nothrow) functor_task_node<std::decay_t<FV>>{ std::forward<FV>(f) };
}

} // namespace sl::exec
