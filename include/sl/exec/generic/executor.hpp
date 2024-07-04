//
// Created by usatiynyan.
// Mostly hoping for devirtualization of executors when concrete ones are used.
//

#pragma once

#include "sl/exec/generic/task.hpp"
#include <libassert/assert.hpp>
#include <tl/expected.hpp>

namespace sl::exec {

struct generic_executor {
    virtual ~generic_executor() noexcept = default;
    virtual bool schedule(generic_task_node* task_node) noexcept = 0;
    virtual void stop() noexcept = 0;
};

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
