//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/result.hpp"
#include "sl/exec/algo/sched/continue_on.hpp"

namespace sl::exec {

constexpr Signal<meta::unit, meta::undefined> auto start_on(executor& executor) {
    return continue_on(executor)(as_signal(meta::result<meta::unit, meta::undefined>{}));
}

} // namespace sl::exec
