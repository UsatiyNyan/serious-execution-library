//
// Created by usatiynyan.
//

#include "sl/exec/sim/stack.hpp"

#include <cstdlib>

namespace sl::exec::sim {

meta::result<stack, std::error_code> stack::allocate(const platform& a_platform, allocate_type args) {
    if (args.at_least_bytes == 0) [[unlikely]] {
        return meta::err(std::make_error_code(std::errc::invalid_argument));
    }

    auto* bytes = static_cast<std::byte*>(std::malloc(args.at_least_bytes));
    if (bytes == nullptr) [[unlikely]] {
        return meta::err(std::make_error_code(std::errc::not_enough_memory));
    }

    return stack{ mem_type{ bytes, args.at_least_bytes } };
}

std::error_code stack::deallocate() {
    auto a_mem = std::exchange(mem_, mem_type{});
    if (a_mem.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    std::free(static_cast<void*>(a_mem.data()));
    return {};
}

} // namespace sl::exec::sim
