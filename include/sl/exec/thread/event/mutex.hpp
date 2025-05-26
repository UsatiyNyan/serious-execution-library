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
    std::mutex m_{};
    std::condition_variable cv_{};
    bool is_set_ = false;
};

} // namespace sl::exec
