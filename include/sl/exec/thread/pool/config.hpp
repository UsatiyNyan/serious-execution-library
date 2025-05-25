//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/monad/maybe.hpp>

#include <cstdint>

namespace sl::exec {

struct thread_pool_config {
    std::uint32_t tcount;

    static meta::maybe<thread_pool_config> hw_limit();
    static thread_pool_config with_hw_limit(std::uint32_t tcount);
};

} // namespace sl::exec
