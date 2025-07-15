//
// Created by usatiynyan.
//
// Based on "Exploring the Efficiency of Multi-Word Compare-and-Swap" by Nodari Kankava
//

#pragma once

#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/multiword_kcas.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"
#include <atomic>

namespace sl::exec::detail {

template <typename T, template <typename> typename Atomic>
struct lock_free_intrusive_list_node {
    T* downcast() { return static_cast<T*>(this); }
    const T* downcast() const { return static_cast<T*>(this); }

    alignas(hardware_destructive_interference_size) Atomic<lock_free_intrusive_list_node*> intrusive_prev{ nullptr };
    alignas(hardware_destructive_interference_size) Atomic<lock_free_intrusive_list_node*> intrusive_next{ nullptr };
};

template <typename T, template <typename> typename Atomic>
struct lock_free_intrusive_list {
    using node_type = lock_free_intrusive_list_node<T, Atomic>;
    struct dummy_type : node_type {};

public:
    lock_free_intrusive_list() {
        head_dummy_.intrusive_prev.store(&head_dummy_, std::memory_order::relaxed);
        tail_dummy_.intrusive_next.store(&tail_dummy_, std::memory_order::relaxed);
    }

    void push_back(node_type& node) & {
        node.intrusive_next.store(&tail_dummy_, std::memory_order::relaxed);

        while (true) {
            node_type* const tail = kcas_read(tail_);
            node_type* const tail_next = kcas_read(tail->intrusive_next);

            if (tail == tail_next) { // is empty
                node.intrusive_prev.store(&head_dummy_, std::memory_order::relaxed);
                node_type* const head = kcas_read(head_);
                if (kcas(
                        kcas_arg{ .a = head_, .e = head, .n = &node }, //
                        kcas_arg{ .a = tail_, .e = tail, .n = &node }
                    )) {
                    break;
                }
            } else {
                node.intrusive_prev.store(tail, std::memory_order::relaxed);
                if (kcas(
                        kcas_arg{ .a = tail_, .e = tail, .n = &node }, //
                        kcas_arg{ .a = tail->intrusive_next, .e = tail_next, .n = &node }
                    )) {
                    break;
                }
            }
        }
    }

    void push_front(node_type& node) & {
        node.intrusive_prev.store(&head_dummy_, std::memory_order::relaxed);

        while (true) {
            node_type* const head = kcas_read(head_);
            node_type* const head_prev = kcas_read(head->intrusive_prev);

            if (head == head_prev) { // is empty
                node.intrusive_next.store(&tail_dummy_, std::memory_order::relaxed);
                node_type* const tail = kcas_read(tail_);
                if (kcas(
                        kcas_arg{ .a = head_, .e = head, .n = &node }, //
                        kcas_arg{ .a = tail_, .e = tail, .n = &node }
                    )) {
                    break;
                }
            } else {
                node.intrusive_next.store(head, std::memory_order::relaxed);
                if (kcas(
                        kcas_arg{ .a = head_, .e = head, .n = &node }, //
                        kcas_arg{ .a = head->intrusive_prev, .e = head_prev, .n = &node }
                    )) {
                    break;
                }
            }
        }
    }

    T* pop_back() & {
        while (true) {
            node_type* const tail = kcas_read(tail_);
            node_type* const tail_prev = kcas_read(tail->intrusive_prev);
            node_type* const tail_next = kcas_read(tail->intrusive_next);

            if (tail == tail_next) { // is empty
                if (kcas_read(tail_) == tail) {
                    return nullptr;
                }
            } else {
                node_type* const nullptr_ = nullptr;
                if (kcas(
                        kcas_arg{ .a = tail_, .e = tail, .n = tail_prev },
                        kcas_arg{ .a = tail_prev->intrusive_next, .e = tail, .n = tail_next },
                        kcas_arg{ .a = tail->intrusive_prev, .e = tail_prev, .n = nullptr_ },
                        kcas_arg{ .a = tail->intrusive_next, .e = tail_next, .n = nullptr_ }
                    )) {
                    return tail->downcast();
                }
            }
        }
    }

    T* pop_front() & {
        while (true) {
            node_type* const head = kcas_read(head_);
            node_type* const head_prev = kcas_read(head->intrusive_prev);
            node_type* const head_next = kcas_read(head->intrusive_next);

            if (head == head_prev) { // is empty
                if (kcas_read(head_) == head) {
                    return nullptr;
                }
            } else if (kcas(
                           kcas_arg{ .a = head_, .e = head, .n = head_next },
                           kcas_arg{ .a = head_next->intrusive_prev, .e = head, .n = head_prev },
                           kcas_arg{ .a = head->intrusive_prev, .e = head_prev, .n = head },
                           kcas_arg{ .a = head->intrusive_next, .e = head_next, .n = head }
                       )) {
                return head->downcast();
            }
        }
    }

    bool insert_before(node_type& next, node_type& node) & {
        if (&next == &head_dummy_) {
            return insert_after(next, node);
        }

        node_type* const prev = kcas_read(next.intrusive_prev);

        if ((prev == nullptr) || (kcas_read(next.intrusive_next) == nullptr && &next != tail_dummy_)) {
            return false;
        }

        node_type* const node_next = kcas_read(node.intrusive_next);
        node_type* const node_prev = kcas_read(node.intrusive_prev);
        return kcas(
            kcas_arg{ .a = prev->intrusive_next, .e = &next, .n = &node },
            kcas_arg{ .a = next.intrusive_prev, .e = prev, .n = &node },
            kcas_arg{ .a = node.intrusive_next, .e = node_next, .n = &next },
            kcas_arg{ .a = node.intrusive_prev, .e = node_prev, .n = prev }
        );
    }

    bool insert_after(node_type& prev, node_type& node) & {
        if (&prev == &tail_dummy_) {
            return insert_before(prev, node);
        }

        node_type* const next = kcas_read(prev.intrusive_next);

        if ((next == nullptr) || (kcas_read(prev.intrusive_prev) == nullptr && &prev != head_dummy_)) {
            return false;
        }

        node_type* const node_next = kcas_read(node.intrusive_next);
        node_type* const node_prev = kcas_read(node.intrusive_prev);
        return kcas(
            kcas_arg{ .a = prev.intrusive_next, .e = next, .n = &node },
            kcas_arg{ .a = next->intrusive_prev, .e = &prev, .n = &node },
            kcas_arg{ .a = node.intrusive_next, .e = node_next, .n = next },
            kcas_arg{ .a = node.intrusive_prev, .e = node_prev, .n = &prev }
        );
    }

    bool delete_node(node_type& node) & {
        if (&node == &head_dummy_ || &node == &tail_dummy_) {
            return true;
        }

        node_type* const prev = kcas_read(node.intrusive_prev);
        node_type* const next = kcas_read(node.intrusive_next);

        if (prev == nullptr && next == nullptr) {
            return false;
        }

        node_type* const nullptr_ = nullptr;
        return kcas(
            kcas_arg{ .a = prev->intrusive_next, .e = &node, .n = next },
            kcas_arg{ .a = next->intrusive_prev, .e = &node, .n = prev },
            kcas_arg{ .a = node.intrusive_next, .e = next, .n = nullptr_ },
            kcas_arg{ .a = node.intrusive_prev, .e = prev, .n = nullptr_ }
        );
    }

    node_type* head_dummy() { return &head_dummy_; }
    node_type* tail_dummy() { return &tail_dummy_; }

    const auto& head() const { return head_; }
    const auto& tail() const { return tail_; }

private:
    dummy_type head_dummy_{};
    dummy_type tail_dummy_{};
    alignas(hardware_destructive_interference_size) Atomic<node_type*> head_{ &head_dummy_ };
    alignas(hardware_destructive_interference_size) Atomic<node_type*> tail_{ &tail_dummy_ };
};

} // namespace sl::exec::detail
