//
// Created by usatiynyan.
//

#include "sl/exec/algo/event/mutex.hpp"

namespace sl::exec {

void mutex_event::set() {
    std::lock_guard lock{ m_ };
    is_set_ = true;
}

void mutex_event::wait() {
    std::unique_lock lock{ m_ };
    while (!is_set_) {
        cv_.wait(lock);
    }
}

} // namespace sl::exec
