//
// Created by usatiynyan.
// Mostly hoping for devirtualization of executors when concrete ones are used.
//

#pragma once

#include "sl/exec/generic/task.hpp"

namespace sl::exec {

struct generic_executor {
    virtual ~generic_executor() noexcept = default;
    virtual void schedule(generic_task_node* task_node) noexcept = 0;
    virtual void stop() noexcept = 0;
};

} // namespace sl::exec
