//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/generic/task.hpp"
#include <libassert/assert.hpp>
#include <tl/expected.hpp>

namespace sl::exec {

struct generic_executor {
    virtual ~generic_executor() noexcept = default;
    virtual bool schedule(generic_task_node* task_node) noexcept = 0;
};

template <typename F>
bool schedule(generic_executor& executor, F&& f) {
    if (auto* node = ASSUME_VAL(allocate_functor_task_node(std::forward<F>(f)))) {
        return executor.schedule(node);
    }
    return false;
}

} // namespace sl::exec
