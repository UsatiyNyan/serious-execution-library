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
namespace detail {

struct subscribe_emit {
    template <SomeSignal SignalT>
    constexpr auto operator()(SignalT&& signal) && noexcept {
        return std::move(signal).subscribe(ForSignal<dummy_slot_ctor, SignalT>{});
    }
};

} // namespace detail

constexpr auto subscribe() { return detail::subscribe_emit{}; }

} // namespace sl::exec
