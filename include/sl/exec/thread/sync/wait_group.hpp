//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/detail/atomic.hpp"

#include <cstdint>

namespace sl::exec {

template <template <typename> typename Atomic>
struct wait_group {
    void add(std::uint32_t count) { work_.fetch_add(count, std::memory_order::relaxed); }

    void done() {
        if (work_.fetch_sub(1, std::memory_order::release) == 1) {
            work_.notify_all();
        }
    }

    void wait() {
        while (true) {
            const std::uint32_t work = work_.load(std::memory_order::acquire);
            if (work == 0) {
                break;
            }
            work_.wait(work, std::memory_order::relaxed);
        }
    }

private:
    Atomic<std::uint32_t> work_{ 0 };
};

} // namespace sl::exec
