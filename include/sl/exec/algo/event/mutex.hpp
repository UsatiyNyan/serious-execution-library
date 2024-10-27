//
// Created by usatiynyan.
//

#pragma once

#include <condition_variable>
#include <mutex>

namespace sl::exec {

struct mutex_event {
    void set();
    void wait();

private:
    bool is_set_ = false;
    std::mutex m_{};
    std::condition_variable cv_{};
};

} // namespace sl::exec
