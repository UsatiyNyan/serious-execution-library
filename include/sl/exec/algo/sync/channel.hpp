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

template <typename V, typename E>
struct channel_slot_callback {
    virtual ~channel_slot_callback() = default;
    virtual void set_value(V&& value) noexcept = 0;
    virtual void set_error(E&& error) noexcept = 0;
    virtual void set_null() noexcept = 0;
    virtual void set_value_skip_done(V&& value) noexcept { set_value(std::move(value)); }
};

template <typename V, typename E, typename SlotCtorT>
struct channel_slot_callback_impl final : channel_slot_callback<V, E> {
    using SlotT = SlotFrom<SlotCtorT>;

    explicit channel_slot_callback_impl(SlotCtorT slot_ctor) : slot_(std::move(slot_ctor)()) {}

    void set_value(V&& value) noexcept override { std::move(slot_).set_value(std::move(value)); }
    void set_error(E&& error) noexcept override { std::move(slot_).set_error(std::move(error)); }
    void set_null() noexcept override { std::move(slot_).set_null(); }

    SlotT& get_slot() & noexcept { return slot_; }

private:
    SlotT slot_;
};

// Specialization for select slots that have get_done() and set_value_skip_done()
template <typename V, typename E, typename SlotCtorT>
    requires requires(SlotFrom<SlotCtorT>& s, V&& v) {
        s.get_done();
        std::move(s).set_value_skip_done(std::move(v));
    }
struct channel_slot_callback_impl<V, E, SlotCtorT> final : channel_slot_callback<V, E> {
    using SlotT = SlotFrom<SlotCtorT>;

    explicit channel_slot_callback_impl(SlotCtorT slot_ctor) : slot_(std::move(slot_ctor)()) {}

    void set_value(V&& value) noexcept override { std::move(slot_).set_value(std::move(value)); }
    void set_error(E&& error) noexcept override { std::move(slot_).set_error(std::move(error)); }
    void set_null() noexcept override { std::move(slot_).set_null(); }
    void set_value_skip_done(V&& value) noexcept override { std::move(slot_).set_value_skip_done(std::move(value)); }

    SlotT& get_slot() & noexcept { return slot_; }

private:
    SlotT slot_;
};

template <typename V, typename Mutex, template <typename> typename Atomic>
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
        send_node(V&& value, channel_slot_callback<meta::unit, meta::unit>& callback)
            : value_{ std::move(value) }, callback_{ callback } {}

        send_node(V&& value, channel_slot_callback<meta::unit, meta::unit>& callback, Atomic<std::size_t>& select_done)
            : value_{ std::move(value) }, callback_{ callback } {
            channel_node::select_done = &select_done;
        }

        V& get_value() & { return value_; }
        channel_slot_callback<meta::unit, meta::unit>& get_callback() & { return callback_; }

    private:
        V value_;
        channel_slot_callback<meta::unit, meta::unit>& callback_;
    };

    struct recv_node
        : channel_node
        , meta::intrusive_list_node<recv_node> {
        explicit recv_node(channel_slot_callback<V, meta::unit>& callback) : callback_{ callback } {}

        recv_node(channel_slot_callback<V, meta::unit>& callback, Atomic<std::size_t>& select_done)
            : callback_{ callback } {
            channel_node::select_done = &select_done;
        }

        channel_slot_callback<V, meta::unit>& get_callback() & { return callback_; }

    private:
        channel_slot_callback<V, meta::unit>& callback_;
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
            a_send_node.get_callback().set_error(meta::unit{});
            return;
        }

        if (a_send_node.requested_cancel) {
            lock.unlock();
            a_send_node.get_callback().set_null();
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

            if (a_recv_node->select_done != nullptr && kcas_read(*a_recv_node->select_done) != 0) {
                // if recv's select is done, then it will be try_cancell-ed by select and we don't need to requeue it,
                // but need to mark it for cancellation (since it is not queued)
                a_recv_node->requested_cancel = true;
            } else {
                // otherwise recv is not on select or it is not done -> failed because of send's select
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
                a_recv_node.get_callback().set_error(meta::unit{});
            } else {
                a_recv_node.queued_in = this;
                recvq_.push_back(&a_recv_node);
            }
        };

        if (a_recv_node.requested_cancel) {
            lock.unlock();
            a_recv_node.get_callback().set_null();
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

            if (a_send_node->select_done != nullptr && kcas_read(*a_send_node->select_done) != 0) {
                // if send's select is done, then it will be try_cancell-ed by select and we don't need to requeue it
                // but need to mark it for cancellation (since it is not queued)
                a_send_node->requested_cancel = true;
            } else {
                // otherwise send is not on select or it is not done -> failed because of recv's select
                // put send back at front
                a_send_node->queued_in = this;
                sendq_.push_front(a_send_node);
                // don't need to queue incoming recv, but need to request cancel - it will be try_cancell-ed by select
                a_recv_node.requested_cancel = true;
                break;
            }
        }
    }

    void close(channel_slot_callback<meta::unit, meta::unit>& close_callback) {
        std::unique_lock<Mutex> lock{ m_ };
        const bool was_closed = std::exchange(is_closed_, true);
        if (was_closed) {
            lock.unlock();
            close_callback.set_error(meta::unit{});
            return;
        }

        auto pending_recvs = std::move(recvq_);

        for (recv_node& node : pending_recvs) {
            node.queued_in = nullptr;
            DEBUG_ASSERT(!node.requested_cancel);
        }

        lock.unlock();

        for (recv_node& node : pending_recvs) {
            node.get_callback().set_error(meta::unit{});
        }

        close_callback.set_value(meta::unit{});
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
                a_recv_node.get_callback().set_value_skip_done(std::move(a_send_node.get_value()));
                a_send_node.get_callback().set_value_skip_done(meta::unit{});
            }

            return success;
        }

        if (a_send_node.select_done != nullptr) {
            const bool success = kcas(kcas_arg<std::size_t>{ .a = a_send_node.select_done, .e = 0, .n = 1 });

            if (success) {
                lock.unlock();
                a_recv_node.get_callback().set_value(std::move(a_send_node.get_value()));
                a_send_node.get_callback().set_value_skip_done(meta::unit{});
            }

            return success;
        }

        if (a_recv_node.select_done != nullptr) {
            const bool success = kcas(kcas_arg<std::size_t>{ .a = a_recv_node.select_done, .e = 0, .n = 1 });

            if (success) {
                lock.unlock();
                a_recv_node.get_callback().set_value_skip_done(std::move(a_send_node.get_value()));
                a_send_node.get_callback().set_value(meta::unit{});
            }

            return success;
        }

        lock.unlock();
        a_recv_node.get_callback().set_value(std::move(a_send_node.get_value()));
        a_send_node.get_callback().set_value(meta::unit{});

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
            node.get_callback().set_null();
        }
    }

private:
    meta::intrusive_list<send_node> sendq_;
    meta::intrusive_list<recv_node> recvq_;
    bool is_closed_ = false;
    Mutex m_{};
};

template <typename V, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_send_signal {
    using impl_type = channel_impl<V, Mutex, Atomic>;

    using value_type = meta::unit;
    using error_type = meta::unit;

    template <typename SlotCtorT>
    struct [[nodiscard]] connection_type final {
        using slot_type = SlotFrom<SlotCtorT>;

        connection_type(SlotCtorT slot_ctor, V&& value, impl_type& impl)
            : callback_{ std::move(slot_ctor) }, node_{ [&] {
                  if constexpr (requires { callback_.get_slot().get_done(); }) {
                      return
                          typename impl_type::send_node{ std::move(value), callback_, callback_.get_slot().get_done() };
                  } else {
                      return typename impl_type::send_node{ std::move(value), callback_ };
                  }
              }() },
              impl_{ impl } {}

        CancelHandle auto emit() && noexcept {
            impl_.send(node_);
            return proxy_cancel_handle{ this };
        }
        std::uintptr_t get_ordering() const noexcept { return std::bit_cast<std::uintptr_t>(&impl_); }
        void try_cancel() && noexcept { impl_.unsend(node_); }

    private:
        channel_slot_callback_impl<value_type, error_type, SlotCtorT> callback_;
        typename impl_type::send_node node_;
        impl_type& impl_;
    };

    V value;
    impl_type& impl;

public:
    template <SlotCtorFor<channel_send_signal> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        return connection_type<SlotCtorT>{ std::move(slot_ctor), std::move(value), impl };
    }

    static executor& get_executor() noexcept { return inline_executor(); }
};

template <typename V, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_receive_signal {
    using impl_type = channel_impl<V, Mutex, Atomic>;

    using value_type = V;
    using error_type = meta::unit;

    template <typename SlotCtorT>
    struct [[nodiscard]] connection_type final {
        using slot_type = SlotFrom<SlotCtorT>;

        connection_type(SlotCtorT slot_ctor, impl_type& impl)
            : callback_{ std::move(slot_ctor) }, node_{ [&] {
                  if constexpr (requires { callback_.get_slot().get_done(); }) {
                      return typename impl_type::recv_node{ callback_, callback_.get_slot().get_done() };
                  } else {
                      return typename impl_type::recv_node{ callback_ };
                  }
              }() },
              impl_{ impl } {}

        CancelHandle auto emit() && noexcept {
            impl_.receive(node_);
            return proxy_cancel_handle{ this };
        }
        std::uintptr_t get_ordering() const noexcept { return std::bit_cast<std::uintptr_t>(&impl_); }
        void try_cancel() && noexcept { impl_.unreceive(node_); }

    private:
        channel_slot_callback_impl<value_type, error_type, SlotCtorT> callback_;
        typename impl_type::recv_node node_;
        impl_type& impl_;
    };

    impl_type& impl;

public:
    template <SlotCtorFor<channel_receive_signal> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        return connection_type<SlotCtorT>{ std::move(slot_ctor), impl };
    }

    static executor& get_executor() noexcept { return inline_executor(); }
};

template <typename V, typename Mutex, template <typename> typename Atomic>
struct [[nodiscard]] channel_close_signal {
    using impl_type = channel_impl<V, Mutex, Atomic>;

    using value_type = meta::unit;
    using error_type = meta::unit;

    template <typename SlotCtorT>
    struct [[nodiscard]] connection_type final {
        connection_type(SlotCtorT slot_ctor, impl_type& impl) : callback_{ std::move(slot_ctor) }, impl_{ impl } {}

        CancelHandle auto emit() && noexcept {
            impl_.close(callback_);
            return dummy_cancel_handle{};
        }

    private:
        channel_slot_callback_impl<value_type, error_type, SlotCtorT> callback_;
        impl_type& impl_;
    };

    impl_type& impl;

public:
    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT slot_ctor) && noexcept {
        return connection_type<SlotCtorT>{ std::move(slot_ctor), impl };
    }

    static executor& get_executor() noexcept { return inline_executor(); }
};

} // namespace detail

template <typename V, typename Mutex = detail::mutex, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] channel final {
    constexpr SomeSignal auto send(V&& value) & {
        return detail::channel_send_signal<V, Mutex, Atomic>{ .value = std::move(value), .impl = impl_ };
    }
    constexpr SomeSignal auto receive() & { return detail::channel_receive_signal<V, Mutex, Atomic>{ .impl = impl_ }; }
    constexpr SomeSignal auto close() & { return detail::channel_close_signal<V, Mutex, Atomic>{ .impl = impl_ }; }

private:
    detail::channel_impl<V, Mutex, Atomic> impl_;
};

template <typename V, typename Mutex = detail::mutex, template <typename> typename Atomic = detail::atomic>
constexpr arc<channel<V, Mutex, Atomic>, Atomic> make_channel() {
    return arc<channel<V, Mutex, Atomic>, Atomic>::make();
}

} // namespace sl::exec
