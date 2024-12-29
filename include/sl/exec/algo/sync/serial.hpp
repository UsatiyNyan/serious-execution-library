//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/executor.hpp"

#include "sl/exec/thread/detail/lock_free_stack.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <atomic>

namespace sl::exec {

struct serial_executor : executor {
    explicit serial_executor(executor& executor) : executor_{ executor } {}

    void schedule(task_node* task_node) noexcept override;
    void stop() noexcept override;

private:
    void schedule_batch_processing();

private:
    alignas(detail::hardware_destructive_interference_size) detail::lock_free_stack<task_node> batch_;
    alignas(detail::hardware_destructive_interference_size) std::atomic<std::uint32_t> work_{ 0 };
    executor& executor_;
};

} // namespace sl::exec
