//
// Created by usatiynyan.
//

#pragma once

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
public:
    struct allocate_type {
        std::size_t at_least_bytes;
    };
    [[nodiscard]] static meta::result<stack, std::error_code> allocate(allocate_type args);
    [[nodiscard]] std::error_code deallocate();

    std::span<std::byte> mem() & { return mem_; }
    std::span<const std::byte> mem() const& { return mem_; }

public:
    stack(const stack&) = delete;
    stack& operator=(const stack&) = delete;

    stack(stack&&) noexcept = default;
    stack& operator=(stack&&) noexcept = default;

    ~stack() noexcept { DEBUG_ASSERT(mem_.empty()); }

private:
    stack(std::span<std::byte> a_mem) : mem_{ a_mem } { DEBUG_ASSERT(!mem_.empty()); }

private:
    std::span<std::byte> mem_;
};

} // namespace sl::exec::sim
