//
// Created by usatiynyan.
//
// for long lived "connections" and manual emits
//
// Example:
// subscribe_connection imalive = signal | subscribe();
// std::move(imalive).emit();
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/connection.hpp"

#include <sl/meta/traits/unique.hpp>
#include <sl/meta/type/undefined.hpp>
#include <sl/meta/type/unit.hpp>

namespace sl::exec {

template <SomeSignal SignalT, std::derived_from<ISlotFor<SignalT>> SlotT>
struct [[nodiscard]] subscribe_connection : connection {
    // lazy construction for certain purposes
    template <std::invocable<> MakeSlotT>
    constexpr subscribe_connection(SignalT signal, MakeSlotT make_slot)
        : slot_{ make_slot() }, connection_{ std::move(signal).subscribe(slot_) } {}

    cancel_handle& emit() && override { return std::move(connection_).emit(); }

    ISlotFor<SignalT>& get_slot() & { return slot_; }
    const ConnectionFor<SignalT>& get_inner() const& { return connection_; }

private:
    SlotT slot_;
    ConnectionFor<SignalT> connection_;
};

namespace detail {

struct subscribe_emit {
    using slot_type = dummy_slot<meta::unit, meta::undefined>;

    template <Signal<meta::unit, meta::undefined> SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return subscribe_connection<SignalT, slot_type>{ std::move(signal), [] { return slot_type{}; } };
    }
};

} // namespace detail

constexpr auto subscribe() { return detail::subscribe_emit{}; }

} // namespace sl::exec
