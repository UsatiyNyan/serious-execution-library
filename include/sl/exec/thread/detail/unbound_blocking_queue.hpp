//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>

#include <libassert/assert.hpp>

#include <condition_variable>
#include <mutex>

namespace sl::exec::detail {

template <typename T>
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
    meta::intrusive_forward_list<T> q_;
    std::mutex m_;
    std::condition_variable event_;
    bool is_closed_ = false;
};

} // namespace sl::exec::detail
