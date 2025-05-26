//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/detail/atomic.hpp"

namespace sl::exec {

template <template <typename> typename Atomic = detail::atomic>
struct atomic_event {
    void set() {
        is_set_.store(1u, std::memory_order::release);
        is_set_.notify_one();
    }

    void wait() {
        while (is_set_.load(std::memory_order::acquire) == 0) {
            is_set_.wait(0, std::memory_order::relaxed);
        }
    }

private:
    Atomic<std::uint32_t> is_set_{ 0u };
};

} // namespace sl::exec
