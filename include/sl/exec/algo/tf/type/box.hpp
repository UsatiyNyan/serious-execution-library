//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/connection.hpp"
#include "sl/exec/model/slot.hpp"

#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/monad/maybe.hpp>

#include <memory>

namespace sl::exec {
namespace detail {

template <typename V, typename E>
struct box_slot {
    struct ctor {
        slot_callback<V, E>& cb;
        constexpr box_slot<V, E> operator()() && noexcept { return { .cb = cb }; }
    };

    using result_type = meta::result<V, E>;

public:
    slot_callback<V, E>& cb;

public:
    void set_value(V&& value) && noexcept { std::move(cb).set_result(result_type{ meta::ok_tag, std::move(value) }); }
    void set_error(E&& error) && noexcept { std::move(cb).set_result(result_type{ meta::err_tag, std::move(error) }); }
    void set_null() && noexcept { std::move(cb).set_result(meta::null); }
};

struct box_cancel_handle final {
    struct base {
        virtual ~base() = default;
        virtual void try_cancel() && noexcept = 0;
    };

    template <CancelHandle CancelHandleT>
    struct impl final : base {
        constexpr explicit impl(CancelHandleT&& cancel_handle) : inner_{ std::move(cancel_handle) } {}

        void try_cancel() && noexcept override { std::move(inner_).try_cancel(); }

    private:
        CancelHandleT inner_;
    };

public:
    std::unique_ptr<base> inner;

    constexpr void try_cancel() && noexcept { std::move(*inner).try_cancel(); }
};

template <typename V, typename E>
struct box_storage_base {
    virtual ~box_storage_base() = default;

    virtual box_cancel_handle emit(slot_callback<V, E>& cb) && noexcept = 0;
};

template <SomeSignal SignalT, typename V = typename SignalT::value_type, typename E = typename SignalT::error_type>
struct box_storage final : box_storage_base<V, E> {
    constexpr explicit box_storage(SignalT&& signal) : signal_{ std::move(signal) } {}

    box_cancel_handle emit(slot_callback<V, E>& cb) && noexcept override {
        using box_slot_ctor = typename box_slot<V, E>::ctor;
        auto& connection = deferred_connection_.emplace(std::move(signal_).subscribe(box_slot_ctor{ cb }));
        CancelHandle auto cancel_handle = std::move(connection).emit();
        using box_cancel_handle_impl = typename box_cancel_handle::impl<decltype(cancel_handle)>;
        return box_cancel_handle{ .inner = std::make_unique<box_cancel_handle_impl>(std::move(cancel_handle)) };
    }

private:
    SignalT signal_;
    meta::maybe<ConnectionFor<SignalT, typename box_slot<V, E>::ctor>> deferred_connection_ = meta::null;
};

template <typename V, typename E, SlotCtor<V, E> SlotCtorT>
struct [[nodiscard]] box_connection final : slot_callback<V, E> {
    using slot_type = SlotFrom<SlotCtorT>;

public:
    box_connection(SlotCtorT&& slot_ctor, std::unique_ptr<box_storage_base<V, E>> storage)
        : slot_{ std::move(slot_ctor)() }, storage_{ std::move(storage) } {}

    CancelHandle auto emit() && noexcept { return std::move(*storage_).emit(*this); }

    void set_result(meta::maybe<meta::result<V, E>>&& maybe_result) && noexcept override {
        fulfill_slot(std::move(slot_), std::move(maybe_result));
    }

private:
    slot_type slot_;
    std::unique_ptr<box_storage_base<V, E>> storage_;
};

} // namespace detail

template <typename V, typename E>
struct [[nodiscard]] box_signal final {
    using value_type = V;
    using error_type = E;

public:
    template <SomeSignal SignalT>
    constexpr explicit box_signal(SignalT&& signal)
        : ex_{ signal.get_executor() }, storage_{ std::make_unique<detail::box_storage<SignalT>>(std::move(signal)) } {}

    template <SlotCtor<V, E> SlotCtorT>
    Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        return detail::box_connection<value_type, error_type, SlotCtorT>{ std::move(slot_ctor), std::move(storage_) };
    }
    executor& get_executor() & noexcept { return ex_; }

private:
    executor& ex_;
    std::unique_ptr<detail::box_storage_base<value_type, error_type>> storage_;
};

template <SomeSignal SignalT>
box_signal(SignalT&& signal) -> box_signal<typename SignalT::value_type, typename SignalT::error_type>;

namespace detail {

struct [[nodiscard]] box final {
    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && noexcept {
        return box_signal{ std::move(signal) };
    }
};

} // namespace detail

constexpr auto box() { return detail::box{}; }

} // namespace sl::exec
