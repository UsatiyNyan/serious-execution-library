//
// Created by usatiynyan.
//

#include "sl/exec/st/executor.hpp"
#include <libassert/assert.hpp>

namespace sl::exec {

st_executor::~st_executor() noexcept {
    if (ASSUME_VAL(queue_.empty(), "st_executor destroyed with unfinished tasks", queue_.size())) {
        return;
    }
    for (auto& task_node : queue_) {
        const auto cleanup = task_node.cancel();
    }
}

void st_executor::schedule(generic_task_node* task_node) noexcept {
    if (ASSUME_VAL(task_node != nullptr) && !is_stopped_) {
        queue_.push_back(task_node);
    }
}

std::size_t st_executor::execute_batch() noexcept {
    generic_task_list batch = std::move(queue_); // clears queue_
    for (auto& task_node : batch) {
        const auto cleanup = task_node.execute();
    }
    return batch.size();
}

std::size_t st_executor::execute_at_most(std::size_t n) noexcept {
    std::size_t counter = 0;
    for (; counter != n; ++counter) {
        auto* task_node = queue_.pop_front();
        if (task_node == nullptr) {
            break;
        }
        const auto cleanup = task_node->execute();
    }
    return counter;
}

} // namespace sl::exec
