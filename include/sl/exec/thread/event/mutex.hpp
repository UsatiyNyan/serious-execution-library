//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/detail/condition_variable.hpp"
#include "sl/exec/thread/detail/mutex.hpp"

namespace sl::exec {

template <typename Mutex = detail::mutex, typename ConditionVariable = detail::condition_variable>
struct mutex_event {
    void set() {
        std::lock_guard lock{ m_ };
        is_set_ = true;
    }
    void wait() {
        std::unique_lock lock{ m_ };
        while (!is_set_) {
            cv_.wait(lock);
        }
    }

private:
    Mutex m_{};
    ConditionVariable cv_{};
    bool is_set_ = false;
};

} // namespace sl::exec
