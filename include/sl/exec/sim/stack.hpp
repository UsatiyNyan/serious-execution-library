//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/sim/platform.hpp"
#include <sl/meta/assert.hpp>
#include <sl/meta/monad/result.hpp>
#include <sl/meta/type/unit.hpp>

#include <cstddef>
#include <span>
#include <system_error>

namespace sl::exec::sim {

// expected use:
//
// auto allocate_result = sim::stack::allocate(...);
// if (!allocate_result.has_value()) { /* handle error */ }
// auto& stack = allocate_result.value();
//
// // use stack
//
// if (const auto dealloc_err = stack.deallocate(); dealloc_err) {
//     PANIC("system failure :(", dealloc_err.message());
// }
//
struct stack {
    using mem_type = std::span<std::byte>;

    struct allocate_type {
        std::size_t at_least_bytes;
    };

public:
    [[nodiscard]] static meta::result<stack, std::error_code> allocate(const platform& a_platform, allocate_type args);
    [[nodiscard]] std::error_code deallocate();

    mem_type view() { return mem_.subspan(prot_offset_); }

public:
    stack(const stack&) = delete;
    stack& operator=(const stack&) = delete;
    stack& operator=(stack&& other) noexcept = delete;

    stack(stack&& other) noexcept
        : mem_{ std::exchange(other.mem_, mem_type{}) }, prot_offset_{ std::exchange(other.prot_offset_, 0) } {}
    ~stack() noexcept { DEBUG_ASSERT(mem_.empty()); }

private:
    constexpr stack(mem_type mem, std::size_t prot_offset) : mem_{ mem }, prot_offset_{ prot_offset } {
        DEBUG_ASSERT(!mem_.empty());
    }

private:
    mem_type mem_;
    std::size_t prot_offset_;
};

namespace detail {

constexpr std::size_t ceil_div(std::size_t value, std::size_t divisor) { return (value + divisor - 1) / divisor; }

} // namespace detail

} // namespace sl::exec::sim
