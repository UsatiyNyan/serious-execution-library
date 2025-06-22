//
// Created by usatiynyan.
//
// `channel` is MPMC(Multi Producer Multi Consumer)
//
// TODO: close, error_type = closed? or set_null == close
//

#pragma once

#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/algo/sync/select.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/arc.hpp"
#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/multiword_kcas.hpp"
#include "sl/exec/thread/detail/mutex.hpp"

#include <sl/meta/intrusive/list.hpp>
#include <sl/meta/lifetime/finalizer.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>
#include <sl/meta/traits/unique.hpp>
#include <sl/meta/type/undefined.hpp>

#include <cstdint>

namespace sl::exec {
namespace detail {

struct channel_node : meta::intrusive_list_node<channel_node> {
    enum class tag { send, receive };

public:
    virtual ~channel_node() = default;
    [[nodiscard]] virtual tag get_tag() const& = 0;
};

template <typename ValueT, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_impl {
    struct send_node_type : channel_node {
        tag get_tag() const& { return tag::send; }

        send_node_type(ValueT&& value, slot<meta::unit, meta::undefined>& slot)
            : value_{ std::move(value) }, slot_{ slot } {}

        send_node_type(ValueT&& value, select_slot<Atomic, meta::unit>& select_slot)
            : value_{ std::move(value) }, select_{ select_slot }, slot_{ select_slot } {}

        ValueT& get_value() & { return value_; }
        meta::maybe<select_slot<Atomic, meta::unit>&>& get_select() & { return select_; }
        slot<meta::unit, meta::undefined>& get_slot() & { return slot_; }

    private:
        ValueT value_;
        meta::maybe<select_slot<Atomic, meta::unit>&> select_{};
        slot<meta::unit, meta::undefined>& slot_;
    };

    struct receive_node_type : channel_node {
        tag get_tag() const& { return tag::receive; }

        explicit receive_node_type(slot<ValueT, meta::undefined>& slot) : slot_{ slot } {}

        explicit receive_node_type(select_slot<Atomic, ValueT>& select_slot)
            : select_{ select_slot }, slot_{ select_slot } {}

        meta::maybe<select_slot<Atomic, ValueT>&>& get_select() & { return select_; }
        slot<ValueT, meta::undefined>& get_slot() & { return slot_; }

    private:
        meta::maybe<select_slot<Atomic, ValueT>&> select_{};
        slot<ValueT, meta::undefined>& slot_;
    };

public:
    void send(send_node_type& send_node) & {
        std::unique_lock<Mutex> lock{ m_ };

        channel_node* q_back = q_.back();

        if (q_back == nullptr) {
            q_.push_back(&send_node);
            return;
        }

        switch (q_back->get_tag()) {
        case channel_node::tag::send: {
            q_.push_back(&send_node);
            break;
        }
        case channel_node::tag::receive: {
            while (true) {
                channel_node* q_front = q_.pop_front();
                if (q_front == nullptr) {
                    q_.push_back(&send_node);
                    break;
                }
                ASSERT(q_front->get_tag() == channel_node::tag::receive);

                auto* receive_node = static_cast<receive_node_type*>(q_front);
                if (try_fulfill_impl(send_node, *receive_node, lock)) {
                    break;
                }
            }
            break;
        }
        default:
            UNREACHABLE();
        }
    }
    void receive(receive_node_type& receive_node) & {
        std::unique_lock<Mutex> lock{ m_ };

        channel_node* q_back = q_.back();

        if (q_back == nullptr) {
            q_.push_back(&receive_node);
            return;
        }

        switch (q_back->get_tag()) {
        case channel_node::tag::send: {
            while (true) {
                channel_node* q_front = q_.pop_front();
                if (q_front == nullptr) {
                    q_.push_back(&receive_node);
                    break;
                }
                ASSERT(q_front->get_tag() == channel_node::tag::send);

                auto* send_node = static_cast<send_node_type*>(q_front);
                if (try_fulfill_impl(*send_node, receive_node, lock)) {
                    break;
                }
            }
            break;
        }
        case channel_node::tag::receive: {
            q_.push_back(&receive_node);
            break;
        }
        default:
            UNREACHABLE();
        }
    }

    [[nodiscard]] bool unsend(send_node_type& send_node) & { return un_impl(send_node); }
    [[nodiscard]] bool unreceive(receive_node_type& receive_node) & { return un_impl(receive_node); }

private:
    [[nodiscard]] static bool
        try_fulfill_impl(send_node_type& send_node, receive_node_type& receive_node, std::unique_lock<Mutex>& lock) {
        if (send_node.get_select().has_value() && receive_node.get_select().has_value()) {
            auto& send_select = send_node.get_select().value();
            auto& receive_select = receive_node.get_select().value();

            const bool success = kcas(
                kcas_arg<std::size_t>{ .a = send_select.get_done(), .e = 0, .n = 1 },
                kcas_arg<std::size_t>{ .a = receive_select.get_done(), .e = 0, .n = 1 }
            );

            if (success) {
                lock.unlock();
                receive_select.set_value_skip_done(std::move(send_node.get_value()));
                send_select.set_value_skip_done(meta::unit{});
            }

            return success;
        }

        if (send_node.get_select().has_value()) {
            auto& send_select = send_node.get_select().value();

            const bool success = kcas(kcas_arg<std::size_t>{ .a = send_select.get_done(), .e = 0, .n = 1 });

            if (success) {
                lock.unlock();
                receive_node.get_slot().set_value(std::move(send_node.get_value()));
                send_select.set_value_skip_done(meta::unit{});
            }

            return success;
        }

        if (receive_node.get_select().has_value()) {
            auto& receive_select = receive_node.get_select().value();
            const bool success = kcas(kcas_arg<std::size_t>{ .a = receive_select.get_done(), .e = 0, .n = 1 });

            if (success) {
                lock.unlock();
                receive_select.set_value_skip_done(std::move(send_node.get_value()));
                send_node.get_slot().set_value(meta::unit{});
            }

            return success;
        }

        lock.unlock();
        receive_node.get_slot().set_value(std::move(send_node.get_value()));
        send_node.get_slot().set_value(meta::unit{});

        return true;
    }

    bool un_impl(channel_node& node) & {
        std::lock_guard<Mutex> lock{ m_ };
        if (node.intrusive_next == nullptr && node.intrusive_prev == nullptr) {
            return false;
        }
        DEBUG_ASSERT(std::find_if(q_.begin(), q_.end(), [&node](channel_node& x) { return &node == &x; }) != q_.end());
        std::ignore = q_.erase(&node);
        return true;
    }

private:
    meta::intrusive_list<channel_node> q_;
    Mutex m_{};
};

template <typename ValueT, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_send_signal {
    using impl_type = channel_impl<ValueT, Mutex, Atomic>;

    using value_type = meta::unit;
    using error_type = meta::undefined;

    struct [[nodiscard]] connection_type : cancel_mixin {
        static constexpr std::uintptr_t ordering_offset = 1;

    public:
        connection_type(ValueT&& value, slot<value_type, error_type>& slot, impl_type& impl)
            : node_{ std::move(value), slot }, impl_{ impl } {
            slot.intrusive_next = this;
        }

        connection_type(ValueT&& value, select_slot<Atomic, value_type>& select_slot, impl_type& impl)
            : node_{ std::move(value), select_slot }, impl_{ impl } {
            select_slot.intrusive_next = this;
        }

    public: // Connection
        cancel_mixin& get_cancel_handle() & { return *this; }
        void emit() && { impl_.send(node_); }

    public: // OrderedConnection
        std::uintptr_t get_ordering() const& { return static_cast<std::uintptr_t>(&impl_); }

    public: // cancel_mixin
        bool try_cancel() & override { return impl_.unsend(node_); }

    private:
        impl_type::send_node_type node_;
        impl_type& impl_;
    };

public:
    constexpr channel_send_signal(ValueT&& value, impl_type& impl) : value_{ std::move(value) }, impl_{ impl } {}

    OrderedConnection auto subscribe(slot<value_type, error_type>& slot) && {
        return connection_type{ std::move(value_), slot, impl_ };
    }
    OrderedConnection auto subscribe(select_slot<Atomic, value_type>& select_slot) && {
        return connection_type{ std::move(value_), select_slot, impl_ };
    }
    executor& get_executor() & { return exec::inline_executor(); }

private:
    ValueT value_;
    impl_type& impl_;
};

template <typename ValueT, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_receive_signal {
    using impl_type = channel_impl<ValueT, Mutex, Atomic>;

    using value_type = ValueT;
    using error_type = meta::undefined;

    struct [[nodiscard]] connection_type : cancel_mixin {
        static constexpr std::uintptr_t ordering_offset = 0;

    public:
        connection_type(slot<value_type, error_type>& slot, impl_type& impl) : node_{ slot }, impl_{ impl } {
            slot.intrusive_next = this;
        }

        connection_type(select_slot<Atomic, value_type>& select_slot, impl_type& impl)
            : node_{ select_slot }, impl_{ impl } {
            select_slot.intrusive_next = this;
        }

    public: // Connection
        cancel_mixin& get_cancel_handle() & { return *this; }
        void emit() && { impl_.receive(node_); }

    public: // OrderedConnection
        std::uintptr_t get_ordering() const& { return static_cast<std::uintptr_t>(&impl_) + ordering_offset; }

    public: // cancel_mixin
        bool try_cancel() & override { return impl_.unreceive(node_); }

    private:
        impl_type::receive_node_type node_;
        impl_type& impl_;
    };

public:
    constexpr explicit channel_receive_signal(impl_type& impl) : impl_{ impl } {}

    OrderedConnection auto subscribe(slot<value_type, error_type>& slot) && { return connection_type{ slot, impl_ }; }
    OrderedConnection auto subscribe(select_slot<Atomic, value_type>& select_slot) && {
        return connection_type{ select_slot, impl_ };
    }
    executor& get_executor() & { return exec::inline_executor(); }

private:
    impl_type& impl_;
};

} // namespace detail

template <typename ValueT, typename Mutex = detail::mutex, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] channel {
    constexpr Signal<meta::unit, meta::undefined> auto send(ValueT&& value) & {
        return detail::channel_send_signal<ValueT, Mutex, Atomic>{ std::move(value), impl_ };
    }
    constexpr Signal<ValueT, meta::undefined> auto receive() & {
        return detail::channel_receive_signal<ValueT, Mutex, Atomic>{ impl_ };
    }

private:
    detail::channel_impl<ValueT, Mutex, Atomic> impl_;
};

template <typename ValueT, typename Mutex = detail::mutex, template <typename> typename Atomic = detail::atomic>
constexpr arc<channel<ValueT, Mutex, Atomic>, Atomic> make_channel() {
    return arc<channel<ValueT, Mutex, Atomic>, Atomic>::make();
}

} // namespace sl::exec
