//
// Created by usatiynyan.
//

#pragma once

#include <tl/optional.hpp>
#include <cstdint>

namespace sl::exec {

struct thread_pool_config {
    std::uint32_t tcount;

    static tl::optional<thread_pool_config> hw_limit();
    static thread_pool_config with_hw_limit(std::uint32_t tcount);
};

} // namespace sl::exec
