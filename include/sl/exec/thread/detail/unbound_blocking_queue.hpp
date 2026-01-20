//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/detail/condition_variable.hpp"
#include "sl/exec/thread/detail/mutex.hpp"

#include <sl/meta/intrusive/forward_list.hpp>

#include <sl/meta/assert.hpp>

namespace sl::exec::detail {

template <typename T, typename Mutex = detail::mutex, typename ConditionVariable = detail::condition_variable>
class unbound_blocking_queue {
public:
    bool push(meta::intrusive_forward_list_node<T>* node) {
        std::lock_guard lock{ m_ };
        if (is_closed_) {
            return false;
        }
        const bool q_was_empty = q_.empty();
        q_.push_back(node);
        if (q_was_empty) {
            event_.notify_one();
        }
        return true;
    }

    meta::intrusive_forward_list_node<T>* try_pop() {
        std::unique_lock lock{ m_ };
        while (q_.empty() && !is_closed_) {
            event_.wait(lock);
        }
        auto* node = q_.pop_front();
        DEBUG_ASSERT(node != nullptr || is_closed_);
        return node;
    }

    void close() {
        std::lock_guard lock{ m_ };
        if (!std::exchange(is_closed_, true)) {
            event_.notify_all();
        }
    }

private:
    meta::intrusive_forward_list<T> q_{};
    Mutex m_{};
    ConditionVariable event_{};
    bool is_closed_ = false;
};

} // namespace sl::exec::detail
