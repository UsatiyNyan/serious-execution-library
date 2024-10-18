//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/conn/concept.hpp"

#include <utility>

namespace sl::exec {
namespace detail {

template <typename SlotT, typename T>
struct [[nodiscard]] value_connection {
    SlotT slot;
    T value;

    void emit() && { std::move(slot).set_result(std::move(value)); }
};

template <typename T>
struct [[nodiscard]] value_signal {
    using result_type = T;

    T value;

    template <Slot<result_type> SlotT>
    Connection auto connect(SlotT slot) && {
        return value_connection<SlotT, T>{ .slot = std::move(slot), .value = std::move(value) };
    }
};

} // namespace detail

template <typename TV>
Signal auto as_signal(TV&& value) {
    using T = std::decay_t<TV>;
    return detail::value_signal<T>{ .value = std::forward<TV>(value) };
}

} // namespace sl::exec
