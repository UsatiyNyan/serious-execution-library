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

#include <sl/meta/traits/unique.hpp>
#include <sl/meta/type/undefined.hpp>
#include <sl/meta/type/unit.hpp>

namespace sl::exec {

template <SomeSignal SignalT, std::derived_from<ISlotFor<SignalT>> SlotT>
struct [[nodiscard]] subscribe_connection : meta::immovable {
    // lazy construction for certain purposes
    template <std::invocable<> MakeSlotT>
    constexpr subscribe_connection(SignalT signal, MakeSlotT make_slot)
        : slot_{ make_slot() }, connection_{ std::move(signal).subscribe(slot_) } {}

    cancel_mixin& get_cancel_handle() & { return slot_; }

    void emit() && { std::move(connection_).emit(); }

private:
    SlotT slot_;
    ConnectionFor<SignalT> connection_;
};

namespace detail {

struct subscribe_emit {
    using slot_type = dummy_slot<meta::unit, meta::undefined>;

    template <Signal<meta::unit, meta::undefined> SignalT>
    constexpr Connection auto operator()(SignalT&& signal) && {
        return subscribe_connection<SignalT, slot_type>{ std::move(signal), [] { return slot_type{}; } };
    }
};

} // namespace detail

constexpr auto subscribe() { return detail::subscribe_emit{}; }

} // namespace sl::exec
