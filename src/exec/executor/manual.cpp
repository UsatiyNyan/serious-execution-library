//
// Created by usatiynyan.
//

#include "sl/exec/algo/sched/manual.hpp"
#include <libassert/assert.hpp>

namespace sl::exec {

manual_executor::~manual_executor() noexcept {
    if (!ASSUME_VAL(task_queue_.empty(), "executor destroyed with unfinished tasks")) {
        stop();
    }
}

void manual_executor::schedule(task_node* task_node) noexcept {
    if (ASSUME_VAL(task_node != nullptr)) {
        task_queue_.push_back(task_node);
    }
}

void manual_executor::stop() noexcept {
    for (auto& task_node : task_queue_) {
        task_node.cancel();
    }
    task_queue_.clear();
}

std::size_t manual_executor::execute_batch() noexcept {
    task_list batch = std::move(task_queue_); // clears task_queue_
    for (auto& task_node : batch) {
        task_node.execute();
    }
    return batch.size();
}

std::size_t manual_executor::execute_at_most(std::size_t n) noexcept {
    std::size_t counter = 0;
    for (; counter != n; ++counter) {
        auto* task_node = task_queue_.pop_front();
        if (task_node == nullptr) {
            break;
        }
        task_node->execute();
    }
    return counter;
}

} // namespace sl::exec
