//
// Created by usatiynyan.
// Injection point for condition_variable-s.
//

#pragma once

#ifndef SL_EXEC_CONDITION_VARIABLE

#include <condition_variable>
#define SL_EXEC_CONDITION_VARIABLE std::condition_variable

#endif // SL_EXEC_CONDITION_VARIABLE 

namespace sl::exec::detail {

using condition_variable = SL_EXEC_CONDITION_VARIABLE;

} // namespace sl::exec::detail
