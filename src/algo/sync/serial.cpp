//
// Created by usatiynyan.
//

#include "sl/exec/algo/sync/serial.hpp"

#include <sl/meta/func/unit.hpp>
#include <sl/meta/intrusive/algorithm.hpp>
#include <sl/meta/monad/result.hpp>

namespace sl::exec {
void serial_executor::schedule(task_node* task_node) noexcept {
    batch_.push(task_node); // release task
    const std::uint32_t prev_work = work_.fetch_add(1, std::memory_order::relaxed);
    if (prev_work == 0) {
        executor_.schedule(&task_);
    }
}

// TODO(@usatiynyan): dunno about that one yet, but seems ok algo
void serial_executor::stop() noexcept {
    auto* head = batch_.extract(); // acquire task
    DEBUG_ASSERT(head != nullptr);

    auto* tail = meta::intrusive_forward_list_node_reverse(head);

    std::size_t batch_size = 0;
    meta::intrusive_forward_list_node_for_each(tail, [&batch_size](task_node* task_node) {
        ++batch_size;
        task_node->cancel();
    });

    const std::uint32_t work_before_batch = work_.fetch_sub(batch_size, std::memory_order::relaxed);
    if (work_before_batch > batch_size) {
        stop();
    }
}

void serial_executor::serial_executor_task::execute() noexcept {
    auto* head = self_.batch_.extract(); // acquire task
    DEBUG_ASSERT(head != nullptr);

    auto* tail = meta::intrusive_forward_list_node_reverse(head);

    std::size_t batch_size = 0;
    meta::intrusive_forward_list_node_for_each(tail, [&batch_size](task_node* task_node) {
        ++batch_size;
        task_node->execute();
    });

    const std::uint32_t work_before_batch = self_.work_.fetch_sub(batch_size, std::memory_order::relaxed);
    if (work_before_batch > batch_size) {
        self_.executor_.schedule(this);
    }
}

} // namespace sl::exec
