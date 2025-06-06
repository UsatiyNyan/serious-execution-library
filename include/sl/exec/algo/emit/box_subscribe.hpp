//
// Created by usatiynyan.
//
// for long lived "connections", emit-s on creation
//
// Example: // need to be stored for signal to be eventually called
// boxed_subscription box_conn = signal | box_subscribe();
//

#pragma once

#include "sl/exec/model/concept.hpp"

#include <sl/meta/traits/unique.hpp>
#include <sl/meta/type/undefined.hpp>
#include <sl/meta/type/unit.hpp>

namespace sl::exec {
namespace detail {

struct [[nodiscard]] boxed_subscribe_connection_base : meta::immovable {
    virtual ~boxed_subscribe_connection_base() = default;

    virtual void emit() && = 0;
};

template <Signal<meta::unit, meta::undefined> SignalT>
struct [[nodiscard]] boxed_subscribe_connection : boxed_subscribe_connection_base {
    constexpr explicit boxed_subscribe_connection(SignalT&& signal)
        : connection_{ std::move(signal).subscribe(slot_) } {}

    void emit() && { std::move(connection_).emit(); }

private:
    dummy_slot<meta::unit, meta::undefined> slot_;
    ConnectionFor<SignalT> connection_;
};

} // namespace detail

struct boxed_subscription : meta::unique {
    template <Signal<meta::unit, meta::undefined> SignalT>
    constexpr explicit boxed_subscription(SignalT&& signal)
        : box_{ std::make_unique<detail::boxed_subscribe_connection<SignalT>>(std::move(signal)) } {
        std::move(*box_).emit();
    }

    void reset() && { box_.reset(); }

private:
    std::unique_ptr<detail::boxed_subscribe_connection_base> box_;
};

namespace detail {

struct box_subscribe_emit {
    template <Signal<meta::unit, meta::undefined> SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return boxed_subscription{ /* .signal = */ std::move(signal) };
    }
};

} // namespace detail

constexpr auto box_subscribe() { return detail::box_subscribe_emit{}; }

} // namespace sl::exec
