//
// Created by usatiynyan.
//
// `channel` is MPMC(Multi Producer Multi Consumer)
//
// TODO: cancel could be deferred
//

#pragma once

#include "sl/exec/algo/sync/select.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/arc.hpp"
#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/multiword_kcas.hpp"
#include "sl/exec/thread/detail/mutex.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/intrusive/list.hpp>
#include <sl/meta/lifetime/finalizer.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>
#include <sl/meta/traits/unique.hpp>

#include <bit>
#include <cstdint>
#include <utility>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_impl {
    struct channel_node : meta::immovable {
        virtual ~channel_node() = default;

    public:
        Atomic<std::size_t>* select_done = nullptr;
        channel_impl* queued_in = nullptr; // protected via channel mutex
        bool requested_cancel = false; // protected via channel mutex
    };

    struct send_node
        : channel_node
        , meta::intrusive_list_node<send_node> {
        send_node(ValueT&& value, slot<meta::unit, meta::unit>& slot) : value_{ std::move(value) }, slot_{ slot } {}

        send_node(ValueT&& value, select_slot<Atomic, meta::unit>& select_slot)
            : value_{ std::move(value) }, slot_{ select_slot }, select_{ select_slot } {
            channel_node::select_done = &select_slot.get_done();
        }

        ValueT& get_value() & { return value_; }
        slot<meta::unit, meta::unit>& get_slot() & { return slot_; }
        meta::maybe<select_slot<Atomic, meta::unit>&>& get_select() & { return select_; }

    private:
        ValueT value_;
        slot<meta::unit, meta::unit>& slot_;
        meta::maybe<select_slot<Atomic, meta::unit>&> select_ = meta::null;
    };

    struct recv_node
        : channel_node
        , meta::intrusive_list_node<recv_node> {
        explicit recv_node(slot<ValueT, meta::unit>& slot) : slot_{ slot } {}

        explicit recv_node(select_slot<Atomic, ValueT>& select_slot) : slot_{ select_slot }, select_{ select_slot } {
            channel_node::select_done = &select_slot.get_done();
        }

        slot<ValueT, meta::unit>& get_slot() & { return slot_; }
        meta::maybe<select_slot<Atomic, ValueT>&>& get_select() & { return select_; }

    private:
        slot<ValueT, meta::unit>& slot_;
        meta::maybe<select_slot<Atomic, ValueT>&> select_ = meta::null;
    };

public:
    void send(send_node& a_send_node) & {
        const auto enqueue_impl = [&] {
            a_send_node.queued_in = this;
            sendq_.push_back(&a_send_node);
        };

        std::unique_lock<Mutex> lock{ m_ };

        if (is_closed_) {
            lock.unlock();
            a_send_node.get_slot().set_error(meta::unit{});
            return;
        }

        if (a_send_node.requested_cancel) {
            lock.unlock();
            a_send_node.get_slot().set_null();
            return;
        }

        if (recv_node* recv_back = recvq_.back(); //
            recv_back == nullptr || is_same_select(a_send_node, *recv_back)) {
            enqueue_impl();
            return;
        }

        while (true) {
            recv_node* a_recv_node = recvq_.pop_front();
            if (a_recv_node == nullptr) {
                enqueue_impl();
                break;
            }
            a_recv_node->queued_in = nullptr;
            if (try_fulfill_impl(a_send_node, *a_recv_node, lock)) {
                // unlocked
                break;
            }
            // kcas failed - handle popped recv_node

            // if recv's select is done, then it will be try_cancell-ed by select and we don't need to requeue it
            // otherwise recv is not on select or it is not done -> failed because of send's select
            if (a_recv_node->select_done == nullptr || kcas_read(*a_recv_node->select_done) == 0) {
                // put recv back at front
                a_recv_node->queued_in = this;
                recvq_.push_front(a_recv_node);
                // don't need to queue incoming send, but need to request cancel - it will be try_cancell-ed by select
                a_send_node.requested_cancel = true;
                break;
            }
        }
    }

    void receive(recv_node& a_recv_node) & {
        std::unique_lock<Mutex> lock{ m_ };

        const auto enqueue_impl = [&] {
            if (is_closed_) {
                lock.unlock();
                a_recv_node.get_slot().set_error(meta::unit{});
            } else {
                a_recv_node.queued_in = this;
                recvq_.push_back(&a_recv_node);
            }
        };

        if (a_recv_node.requested_cancel) {
            lock.unlock();
            a_recv_node.get_slot().set_null();
            return;
        }

        if (send_node* send_back = sendq_.back(); //
            send_back == nullptr || is_same_select(*send_back, a_recv_node)) {
            enqueue_impl();
            return;
        }

        while (true) {
            send_node* a_send_node = sendq_.pop_front();
            if (a_send_node == nullptr) {
                enqueue_impl();
                break;
            }
            a_send_node->queued_in = nullptr;
            if (try_fulfill_impl(*a_send_node, a_recv_node, lock)) {
                // unlocked
                break;
            }
            // kcas failed - handle popped send_node

            // if send's select is done, then it will be try_cancell-ed by select and we don't need to requeue it
            // otherwise send is not on select or it is not done -> failed because of recv's select
            if (a_send_node->select_done == nullptr || kcas_read(*a_send_node->select_done) == 0) {
                // put send back at front
                a_send_node->queued_in = this;
                sendq_.push_front(a_send_node);
                // don't need to queue incoming recv, but need to request cancel - it will be try_cancell-ed by select
                a_recv_node.requested_cancel = true;
                break;
            }
        }
    }

    void close(slot<meta::unit, meta::unit>& close_slot) {
        std::unique_lock<Mutex> lock{ m_ };
        const bool was_closed = std::exchange(is_closed_, true);
        if (was_closed) {
            lock.unlock();
            close_slot.set_error(meta::unit{});
            return;
        }

        auto pending_recvs = std::move(recvq_);

        for (recv_node& node : pending_recvs) {
            node.queued_in = nullptr;
            DEBUG_ASSERT(!node.requested_cancel);
        }

        lock.unlock();

        for (recv_node& node : pending_recvs) {
            node.get_slot().set_error(meta::unit{});
        }

        close_slot.set_value(meta::unit{});
    }

    void unsend(send_node& a_node) & { un_impl(sendq_, a_node); }
    void unreceive(recv_node& a_node) & { un_impl(recvq_, a_node); }

private:
    static bool is_same_select(send_node& a_send_node, recv_node& a_recv_node) {
        return a_send_node.select_done != nullptr && a_send_node.select_done == a_recv_node.select_done;
    }

    [[nodiscard]] static bool
        try_fulfill_impl(send_node& a_send_node, recv_node& a_recv_node, std::unique_lock<Mutex>& lock) {
        if (a_send_node.select_done != nullptr && a_recv_node.select_done != nullptr) {
            DEBUG_ASSERT(!is_same_select(a_send_node, a_recv_node));
            const bool success = !is_same_select(a_send_node, a_recv_node)
                                 && kcas(
                                     kcas_arg<std::size_t>{ .a = a_send_node.select_done, .e = 0, .n = 1 },
                                     kcas_arg<std::size_t>{ .a = a_recv_node.select_done, .e = 0, .n = 1 }
                                 );

            if (success) {
                lock.unlock();
                a_recv_node.get_select()->set_value_skip_done(std::move(a_send_node.get_value()));
                a_send_node.get_select()->set_value_skip_done(meta::unit{});
            }

            return success;
        }

        if (a_send_node.select_done != nullptr) {
            const bool success = kcas(kcas_arg<std::size_t>{ .a = a_send_node.select_done, .e = 0, .n = 1 });

            if (success) {
                lock.unlock();
                a_recv_node.get_slot().set_value(std::move(a_send_node.get_value()));
                a_send_node.get_select()->set_value_skip_done(meta::unit{});
            }

            return success;
        }

        if (a_recv_node.select_done != nullptr) {
            const bool success = kcas(kcas_arg<std::size_t>{ .a = a_recv_node.select_done, .e = 0, .n = 1 });

            if (success) {
                lock.unlock();
                a_recv_node.get_select()->set_value_skip_done(std::move(a_send_node.get_value()));
                a_send_node.get_slot().set_value(meta::unit{});
            }

            return success;
        }

        lock.unlock();
        a_recv_node.get_slot().set_value(std::move(a_send_node.get_value()));
        a_send_node.get_slot().set_value(meta::unit{});

        return true;
    }

    template <typename QueueT, typename NodeT>
    void un_impl(QueueT& q, NodeT& node) {
        std::unique_lock<Mutex> lock{ m_ };
        if (node.queued_in != nullptr) {
            DEBUG_ASSERT(node.queued_in == this);
            DEBUG_ASSERT(
                std::find_if(q.begin(), q.end(), [&node](auto& x) { return &node == &x; }) != q.end(),
                "not found node in q_, q_ is ",
                q.empty() ? "empty" : "not empty"
            );
            std::ignore = q.erase(&node);
            node.queued_in = nullptr;
            node.requested_cancel = true;
        }

        if (std::exchange(node.requested_cancel, true)) {
            lock.unlock();
            node.get_slot().set_null();
        }
    }

private:
    meta::intrusive_list<send_node> sendq_;
    meta::intrusive_list<recv_node> recvq_;
    bool is_closed_ = false;
    Mutex m_{};
};

template <typename ValueT, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_send_signal {
    using impl_type = channel_impl<ValueT, Mutex, Atomic>;

    using value_type = meta::unit;
    using error_type = meta::unit;

    struct [[nodiscard]] connection_type final
        : ordered_connection
        , cancel_handle {
    public:
        connection_type(ValueT&& value, slot<value_type, error_type>& slot, impl_type& impl)
            : node_{ std::move(value), slot }, impl_{ impl } {}

        connection_type(ValueT&& value, select_slot<Atomic, value_type>& select_slot, impl_type& impl)
            : node_{ std::move(value), select_slot }, impl_{ impl } {}

    public: // ordered_connection
        cancel_handle& emit() && override {
            impl_.send(node_);
            return *this;
        }
        std::uintptr_t get_ordering() const& override { return std::bit_cast<std::uintptr_t>(&impl_); }

    public: // cancel_handle
        void try_cancel() & override { impl_.unsend(node_); }

    private:
        typename impl_type::send_node node_;
        impl_type& impl_;
    };

public:
    constexpr channel_send_signal(ValueT&& value, impl_type& impl) : value_{ std::move(value) }, impl_{ impl } {}

    connection_type subscribe(slot<value_type, error_type>& slot) && {
        return connection_type{ std::move(value_), slot, impl_ };
    }
    connection_type subscribe(select_slot<Atomic, value_type>& select_slot) && {
        return connection_type{ std::move(value_), select_slot, impl_ };
    }
    executor& get_executor() & { return inline_executor(); }

private:
    ValueT value_;
    impl_type& impl_;
};

template <typename ValueT, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_receive_signal {
    using impl_type = channel_impl<ValueT, Mutex, Atomic>;

    using value_type = ValueT;
    using error_type = meta::unit;

    struct [[nodiscard]] connection_type final
        : ordered_connection
        , cancel_handle {
    public:
        connection_type(slot<value_type, error_type>& slot, impl_type& impl) : node_{ slot }, impl_{ impl } {}

        connection_type(select_slot<Atomic, value_type>& select_slot, impl_type& impl)
            : node_{ select_slot }, impl_{ impl } {}

    public: // ordered_connection
        cancel_handle& emit() && override {
            impl_.receive(node_);
            return *this;
        }
        std::uintptr_t get_ordering() const& override { return std::bit_cast<std::uintptr_t>(&impl_); }

    public: // cancel_handle
        void try_cancel() & override { impl_.unreceive(node_); }

    private:
        typename impl_type::recv_node node_;
        impl_type& impl_;
    };

public:
    constexpr explicit channel_receive_signal(impl_type& impl) : impl_{ impl } {}

    connection_type subscribe(slot<value_type, error_type>& slot) && { return connection_type{ slot, impl_ }; }
    connection_type subscribe(select_slot<Atomic, value_type>& select_slot) && {
        return connection_type{ select_slot, impl_ };
    }
    executor& get_executor() & { return inline_executor(); }

private:
    impl_type& impl_;
};

template <typename ValueT, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_close_signal {
    using impl_type = channel_impl<ValueT, Mutex, Atomic>;

    using value_type = meta::unit;
    using error_type = meta::unit;

    struct [[nodiscard]] connection_type final : connection {
        connection_type(slot<value_type, error_type>& slot, impl_type& impl) : slot_{ slot }, impl_{ impl } {}

    public: // connection
        cancel_handle& emit() && override {
            impl_.close(slot_);
            return dummy_cancel_handle();
        }

    private:
        slot<value_type, error_type>& slot_;
        impl_type& impl_;
    };

public:
    constexpr explicit channel_close_signal(impl_type& impl) : impl_{ impl } {}

    connection_type subscribe(slot<value_type, error_type>& slot) && { return connection_type{ slot, impl_ }; }
    executor& get_executor() & { return inline_executor(); }

private:
    impl_type& impl_;
};

} // namespace detail

template <typename ValueT, typename Mutex = detail::mutex, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] channel final {
    constexpr Signal<meta::unit, meta::unit> auto send(ValueT&& value) & {
        return detail::channel_send_signal<ValueT, Mutex, Atomic>{ std::move(value), impl_ };
    }
    constexpr Signal<ValueT, meta::unit> auto receive() & {
        return detail::channel_receive_signal<ValueT, Mutex, Atomic>{ impl_ };
    }
    constexpr Signal<meta::unit, meta::unit> auto close() & {
        return detail::channel_close_signal<ValueT, Mutex, Atomic>{ impl_ };
    }

private:
    detail::channel_impl<ValueT, Mutex, Atomic> impl_;
};

template <typename ValueT, typename Mutex = detail::mutex, template <typename> typename Atomic = detail::atomic>
constexpr arc<channel<ValueT, Mutex, Atomic>, Atomic> make_channel() {
    return arc<channel<ValueT, Mutex, Atomic>, Atomic>::make();
}

} // namespace sl::exec
