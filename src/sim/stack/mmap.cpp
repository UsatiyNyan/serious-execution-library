//
// Created by usatiynyan.
//

#include "sl/exec/sim/stack.hpp"

#include <sys/mman.h>

namespace sl::exec::sim {

meta::result<stack, std::error_code> stack::allocate(const platform& a_platform, allocate_type args) {
    if (args.at_least_bytes == 0) [[unlikely]] {
        return meta::err(std::make_error_code(std::errc::invalid_argument));
    }

    const std::size_t user_pages = detail::ceil_div(args.at_least_bytes, a_platform.page_size);
    constexpr std::size_t prot_pages = 1;
    const std::size_t size_bytes = (user_pages + prot_pages) * a_platform.page_size;
    const std::size_t prot_offset = prot_pages * a_platform.page_size;

    auto* mmap_bytes = static_cast<std::byte*>(::mmap(
        /*addr=*/0,
        /*length=*/size_bytes,
        /*prot=*/PROT_READ | PROT_WRITE,
        /*flags=*/MAP_PRIVATE | MAP_ANONYMOUS,
        /*fd=*/-1,
        /*offset=*/0
    ));
    if (mmap_bytes == MAP_FAILED) [[unlikely]] {
        return meta::errno_err();
    }

    stack a_stack{ mem_type{ mmap_bytes, size_bytes }, prot_offset };

    const int mprotect_result = ::mprotect(
        /*addr=*/mmap_bytes,
        /*len=*/prot_offset,
        /*prot=*/PROT_NONE
    );
    if (mprotect_result == -1) [[unlikely]] {
        const auto dealloc_err = a_stack.deallocate();
        if (dealloc_err) [[unlikely]] {
            PANIC("failed to recover mprotect:", dealloc_err.message());
        }
        return meta::errno_err();
    }

    return a_stack;
}

std::error_code stack::deallocate() {
    auto a_mem = std::exchange(mem_, mem_type{});
    if (a_mem.empty()) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    const int result = ::munmap(static_cast<void*>(a_mem.data()), a_mem.size());
    if (result == -1) [[unlikely]] {
        return meta::errno_code();
    }

    return {};
}

} // namespace sl::exec::sim
