//
// Created by usatiynyan.
// TODO: think about stop() for an interface
//

#pragma once

#include "sl/exec/executor/task.hpp"

namespace sl::exec {

struct executor {
    virtual ~executor() noexcept = default;
    virtual void schedule(task_node* task_node) noexcept = 0;
};

} // namespace sl::exec
