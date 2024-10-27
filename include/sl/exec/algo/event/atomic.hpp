//
// Created by usatiynyan.
//

#pragma once

#include <atomic>

namespace sl::exec {

struct atomic_event {
    void set();
    void wait();

private:
    std::atomic<std::uint32_t> is_set_{ 0u };
};

} // namespace sl::exec
