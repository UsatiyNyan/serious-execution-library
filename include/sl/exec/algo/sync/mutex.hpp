//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/result.hpp"
#include "sl/exec/algo/sched/continue_on.hpp"
#include "sl/exec/algo/sched/start_on.hpp"
#include "sl/exec/algo/sync/serial.hpp"

namespace sl::exec {

struct [[nodiscard]] mutex_lock {
    constexpr explicit mutex_lock(executor& executor) : executor_{ executor } {}

    constexpr Signal auto unlock() { return start_on(executor_); }
    constexpr Signal auto unlock_on(executor& executor) { return start_on(executor); }

private:
    executor& executor_;
};

struct [[nodiscard]] mutex {
    constexpr explicit mutex(executor& executor) : serial_executor_{ executor } {}

    constexpr Signal auto lock() {
        return value_as_signal(mutex_lock{
                   /* .executor= */ serial_executor_.get_inner(),
               })
               | continue_on(serial_executor_);
    }

private:
    serial_executor serial_executor_;
};

} // namespace sl::exec
