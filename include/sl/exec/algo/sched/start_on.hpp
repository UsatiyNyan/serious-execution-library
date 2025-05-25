//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/result.hpp"
#include "sl/exec/algo/sched/continue_on.hpp"
#include "sl/exec/model/executor.hpp"
#include "sl/exec/model/syntax.hpp"

namespace sl::exec {

constexpr Signal<meta::unit, meta::undefined> auto start_on(executor& executor) {
    return as_signal(meta::result<meta::unit, meta::undefined>{}) | continue_on(executor);
}

} // namespace sl::exec
