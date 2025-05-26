//
// Created by usatiynyan.
// Injection point for atomics.
//

#pragma once

#ifndef SL_EXEC_ATOMIC

#include <atomic>
#define SL_EXEC_ATOMIC std::atomic

#endif // SL_EXEC_ATOMIC

namespace sl::exec::detail {

template <typename T>
using atomic = SL_EXEC_ATOMIC<T>;

} // namespace sl::exec::detail
