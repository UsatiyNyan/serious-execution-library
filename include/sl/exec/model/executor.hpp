//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/task.hpp"

namespace sl::exec {

struct executor {
    virtual ~executor() noexcept = default;
    virtual void schedule(task_node* task_node) noexcept = 0;
    virtual void stop() noexcept = 0;
};

} // namespace sl::exec
