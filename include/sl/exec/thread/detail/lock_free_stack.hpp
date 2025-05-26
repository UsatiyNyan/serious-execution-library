//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/detail/atomic.hpp"

#include <sl/meta/intrusive/forward_list.hpp>

namespace sl::exec::detail {

template <typename T, template <typename> typename Atomic = detail::atomic>
struct lock_free_stack {
    using node_type = meta::intrusive_forward_list_node<T>;

    static void push(Atomic<node_type*>& head, node_type* new_node) {
        new_node->intrusive_next = head.load(std::memory_order::relaxed);

        while (!head.compare_exchange_weak(
            new_node->intrusive_next, new_node, std::memory_order::release, std::memory_order::relaxed
        )) {}
    }
    static node_type* extract(Atomic<node_type*>& head) {
        node_type* old_head = head.load(std::memory_order::relaxed);

        while (old_head != nullptr
               && !head.compare_exchange_weak(old_head, nullptr, std::memory_order::acquire, std::memory_order::relaxed)
        ) {}

        return old_head;
    }

    void push(node_type* new_node) { push(head_, new_node); }
    node_type* extract() { return extract(head_); }

private:
    Atomic<node_type*> head_{ nullptr };
};

} // namespace sl::exec::detail
