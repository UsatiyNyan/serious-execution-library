//
// Created by usatiynyan.
//

#include "sl/exec/thread/sync/wait_group.hpp"

namespace sl::exec {

void wait_group::add(std::uint32_t count) { work_.fetch_add(count, std::memory_order::relaxed); }

void wait_group::done() {
    if (work_.fetch_sub(1, std::memory_order::release) == 1) {
        work_.notify_all();
    }
}

void wait_group::wait() {
    while (true) {
        const std::uint32_t work = work_.load(std::memory_order::acquire);
        if (work == 0) {
            break;
        }
        work_.wait(work, std::memory_order::relaxed);
    }
}

} // namespace sl::exec
