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

    const std::size_t size_bytes = detail::ceil_div(args.at_least_bytes, a_platform.page_size) * a_platform.page_size;
    auto* bytes = static_cast<std::byte*>(std::malloc(size_bytes));
    if (bytes == nullptr) [[unlikely]] {
        return meta::err(std::make_error_code(std::errc::not_enough_memory));
    }

    return stack{ mem_type{ bytes, size_bytes }, /*prot_offset=*/0 };
}

std::error_code stack::deallocate() {
    auto mem = std::exchange(mem_, mem_type{});
    if (mem.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    std::free(static_cast<void*>(mem.data()));
    return {};
}

} // namespace sl::exec::sim
