//
// Created by usatiynyan.
//

#include "sl/exec/sim/platform.hpp"

#if SL_OS_IS_linux

#include <unistd.h>

namespace sl::exec::sim {

meta::result<platform, std::error_code> platform::make() {
    errno = 0;
    const int page_size_result = ::sysconf(_SC_PAGESIZE); // never indeterminate
    if (page_size_result < 0) [[unlikely]] {
        return meta::errno_err();
    }

    return platform {
        .page_size = static_cast<std::size_t>(page_size_result)
    };
}

} // namespace sl::exec::sim

#else

#error "not implemented"

#endif
