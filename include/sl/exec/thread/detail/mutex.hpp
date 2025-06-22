//
// Created by usatiynyan.
// Injection point for mutex-s.
//

#pragma once

#ifndef SL_EXEC_MUTEX

#include <mutex>
#define SL_EXEC_MUTEX std::mutex

#endif // SL_EXEC_MUTEX

namespace sl::exec::detail {

using mutex = SL_EXEC_MUTEX;

} // namespace sl::exec::detail
