//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/multiword_kcas.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/traits/unique.hpp>

#include <atomic>
#include <cstdint>

namespace sl::exec::detail {

template <typename T, template <typename> typename Atomic>
struct lock_free_deque_node {
    T* downcast() { return static_cast<T*>(this); }
    const T* downcast() const { return static_cast<T*>(this); }

    // or on heap :(
    ~lock_free_deque_node() noexcept {
        while (true) {
            const std::uint32_t refcount_value = refcount.load(std::memory_order::relaxed);
            if (refcount_value == 0) {
                break;
            }
            refcount.wait(refcount_value, std::memory_order::relaxed);
        }
    }

public:
    alignas(hardware_destructive_interference_size) Atomic<lock_free_deque_node*> next{ nullptr };
    alignas(hardware_destructive_interference_size) Atomic<lock_free_deque_node*> prev{ nullptr };
    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> refcount{ 0 };
};

//             tail -prev> node -prev> head -prev> head
// tail <next- tail <next- node <next- head
template <typename T, template <typename> typename Atomic = detail::atomic>
struct lock_free_deque : meta::immovable {
    using node_type = lock_free_deque_node<T, Atomic>;

public:
    explicit lock_free_deque() {
        head.next.store(&head, std::memory_order::relaxed);
        head.prev.store(&tail, std::memory_order::relaxed);
        tail.next.store(&head, std::memory_order::relaxed);
        tail.prev.store(&tail, std::memory_order::relaxed);
    }

    void push_back(node_type* node) {
        DEBUG_ASSERT(
            node != nullptr //
            && node->prev.load(std::memory_order::relaxed) == nullptr
            && node->next.load(std::memory_order::relaxed) == nullptr
        );

        while (true) {
            node_type* tail_prev = kcas_read(tail.prev);
            // tail_prev refcount +1
            // defer tail_prev refcount -1
            node_type* tail_prev_next = kcas_read(tail_prev->next);
            if (kcas(
                    kcas_arg<node_type*>{ .a = tail.prev, .e = tail_prev, .n = node },
                    kcas_arg<node_type*>{ .a = tail_prev->next, .e = tail_prev_next, .n = node },
                    kcas_arg<node_type*>{ .a = node->prev, .e = nullptr, .n = tail_prev },
                    kcas_arg<node_type*>{ .a = node->next, .e = nullptr, .n = &tail }
                )) {
                break;
            }
        }
    }

    T* pop_front() {
        while (true) {
            node_type* head_next = kcas_read(head.next);
            if (head_next == &tail) { // empty
                return nullptr;
            }
            // head_next refcount +1
            // defer head_next refcount -1
            node_type* head_next_next = kcas_read(head_next->next);
            // head_next_next refcount +1
            // defer head_next_next refcount -1
            if (kcas(
                    kcas_arg<node_type*>{ .a = head.next, .e = head_next, .n = head_next_next },
                    kcas_arg<node_type*>{ .a = head_next_next->prev, .e = head_next, .n = &head },
                    kcas_arg<node_type*>{ .a = head_next->next, .e = &head, .n = nullptr }
                )) {
                return head_next->downcast();
            }
        }
    }

private:
    node_type head;
    node_type tail;
};

} // namespace sl::exec::detail
