//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>

#include <atomic>

namespace sl::exec::detail {

template <typename T>
struct lock_free_stack {
    using node_type = meta::intrusive_forward_list_node<T>;

    void push(node_type* new_node) {
        new_node->intrusive_next = head_.load(std::memory_order::relaxed);

        while (!head_.compare_exchange_weak(
            new_node->intrusive_next, new_node, std::memory_order::release, std::memory_order::relaxed
        )) {}
    }

    node_type* extract() {
        node_type* old_head = head_.load(std::memory_order::relaxed);

        while (
            old_head != nullptr
            && !head_.compare_exchange_weak(old_head, nullptr, std::memory_order::acquire, std::memory_order::relaxed)
        ) {}

        return old_head;
    }

private:
    std::atomic<node_type*> head_{ nullptr };
};

} // namespace sl::exec::detail
