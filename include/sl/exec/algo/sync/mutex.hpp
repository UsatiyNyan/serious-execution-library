//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/result.hpp"
#include "sl/exec/algo/make/schedule.hpp"
#include "sl/exec/algo/sched/on.hpp"
#include "sl/exec/algo/sync/serial.hpp"
#include "sl/exec/model/concept.hpp"

#include <tl/expected.hpp>

namespace sl::exec {

struct [[nodiscard]] mutex_lock {
    // TODO: warn about not unlocked mutex_lock

    Signal auto unlock_on(executor& executor) { return schedule(executor); }
};

struct [[nodiscard]] mutex {
    explicit mutex(executor& executor) : serial_executor_{ executor } {}

    Signal auto lock() { return value_as_signal(mutex_lock{}) | on(serial_executor_); }

private:
    serial_executor serial_executor_;
};

} // namespace sl::exec
