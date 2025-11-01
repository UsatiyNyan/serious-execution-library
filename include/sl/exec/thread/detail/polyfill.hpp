//
// Created by usatiynyan.
//

#pragma once

#if !SL_EXEC_INTERFERENCE_SIZE
#ifdef __cpp_lib_hardware_interference_size
#include <new>
#endif
#endif

#include <cstdint>

namespace sl::exec::detail {

#if !SL_EXEC_INTERFERENCE_SIZE
#ifdef __cpp_lib_hardware_interference_size
using std::hardware_constructive_interference_size;
using std::hardware_destructive_interference_size;
#elif defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
constexpr std::size_t hardware_constructive_interference_size = 64;
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif
#else
constexpr std::size_t hardware_constructive_interference_size = SL_EXEC_INTERFERENCE_SIZE;
constexpr std::size_t hardware_destructive_interference_size = SL_EXEC_INTERFERENCE_SIZE;
#endif

} // namespace sl::exec::detail
