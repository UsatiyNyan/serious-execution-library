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

    static void push(std::atomic<node_type*>& head, node_type* new_node) {
        new_node->intrusive_next = head.load(std::memory_order::relaxed);

        while (!head.compare_exchange_weak(
            new_node->intrusive_next, new_node, std::memory_order::release, std::memory_order::relaxed
        )) {}
    }
    static node_type* extract(std::atomic<node_type*>& head) {
        node_type* old_head = head.load(std::memory_order::relaxed);

        while (old_head != nullptr
               && !head.compare_exchange_weak(old_head, nullptr, std::memory_order::acquire, std::memory_order::relaxed)
        ) {}

        return old_head;
    }

    void push(node_type* new_node) { push(head_, new_node); }
    node_type* extract() { return extract(head_); }

private:
    std::atomic<node_type*> head_{ nullptr };
};

} // namespace sl::exec::detail
