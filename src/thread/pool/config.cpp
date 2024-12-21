//
// Created by usatiynyan.
//

#include "sl/exec/thread/pool/config.hpp"

#include <libassert/assert.hpp>

#include <thread>

namespace sl::exec {

tl::optional<thread_pool_config> thread_pool_config::hw_limit() {
    const std::uint32_t hwc = std::thread::hardware_concurrency();
    if (hwc == 0) {
        return tl::nullopt;
    }
    return thread_pool_config{ .tcount = hwc };
}

thread_pool_config thread_pool_config::with_hw_limit(std::uint32_t tcount) {
    ASSERT(tcount > 0);
    const std::uint32_t hwc = std::thread::hardware_concurrency();
    return thread_pool_config{
        .tcount = (hwc == 0 ? tcount : std::min(tcount, hwc)),
    };
}

} // namespace sl::exec
