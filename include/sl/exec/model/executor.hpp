//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/task.hpp"

namespace sl::exec {

struct executor {
    virtual ~executor() noexcept = default;
    virtual void schedule(task_node& a_task_node) noexcept = 0;
    virtual void stop() noexcept = 0;
};

inline executor& inline_executor() {
    struct impl final : executor {
        constexpr void schedule(task_node& a_task_node) noexcept override { a_task_node.execute(); }
        constexpr void stop() noexcept override {}
    };

    static impl an_executor;
    return an_executor;
}

} // namespace sl::exec
