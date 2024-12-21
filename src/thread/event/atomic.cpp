//
// Created by usatiynyan.
//

#include "sl/exec/thread/event/atomic.hpp"

namespace sl::exec {

void atomic_event::set() {
    is_set_.store(1u, std::memory_order::release);
    is_set_.notify_one();
}

void atomic_event::wait() {
    while (is_set_.load(std::memory_order::acquire) == 0) {
        is_set_.wait(0, std::memory_order::relaxed);
    }
}

} // namespace sl::exec
