//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/executor.hpp"

#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/lock_free_stack.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/intrusive/algorithm.hpp>

namespace sl::exec {

template <template <typename> typename Atomic = detail::atomic>
struct serial_executor : executor {
private:
    struct serial_executor_task : task_node {
        constexpr explicit serial_executor_task(serial_executor& self) : self_{ self } {}

        void execute() noexcept override {
            auto* head = self_.batch_.extract(); // acquire task
            DEBUG_ASSERT(head != nullptr);

            auto* tail = meta::intrusive_forward_list_node_reverse(head);

            std::size_t batch_size = 0;
            meta::intrusive_forward_list_node_for_each(tail, [&batch_size](task_node* a_task_node) {
                ++batch_size;
                a_task_node->execute();
            });

            const std::uint32_t work_before_batch = self_.work_.fetch_sub(batch_size, std::memory_order::relaxed);
            if (work_before_batch > batch_size) {
                self_.executor_.schedule(this);
            }
        }
        void cancel() noexcept override { self_.stop(); }

    private:
        serial_executor& self_;
    };

public:
    constexpr explicit serial_executor(executor& an_executor) : task_{ *this }, executor_{ an_executor } {}

    void schedule(task_node* task_node) noexcept override {
        batch_.push(task_node); // release task
        const std::uint32_t prev_work = work_.fetch_add(1, std::memory_order::relaxed);
        if (prev_work == 0) {
            executor_.schedule(&task_);
        }
    }

    // TODO(@UsatiyNyan): dunno about that one yet, but seems like an ok algorithm
    void stop() noexcept override {
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

    constexpr executor& get_inner() const { return executor_; }

private:
    serial_executor_task task_;
    alignas(detail::hardware_destructive_interference_size) detail::lock_free_stack<task_node, Atomic> batch_;
    alignas(detail::hardware_destructive_interference_size) Atomic<std::uint32_t> work_{ 0 };
    executor& executor_;
};

} // namespace sl::exec
