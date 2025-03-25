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
private:
    struct serial_executor_task : task_node {
        constexpr explicit serial_executor_task(serial_executor& self) : self_{ self } {}

        void execute() noexcept override;
        void cancel() noexcept override { self_.stop(); }

    private:
        serial_executor& self_;
    };

public:
    constexpr explicit serial_executor(executor& executor) : task_{ *this }, executor_{ executor } {}

    void schedule(task_node* task_node) noexcept override;
    void stop() noexcept override;

    constexpr executor& get_inner() const { return executor_; }

private:
    serial_executor_task task_;
    alignas(detail::hardware_destructive_interference_size) detail::lock_free_stack<task_node> batch_;
    alignas(detail::hardware_destructive_interference_size) std::atomic<std::uint32_t> work_{ 0 };
    executor& executor_;
};

} // namespace sl::exec
