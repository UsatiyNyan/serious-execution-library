//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/model/concept.hpp"

#include <utility>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, Slot<ValueT, ErrorT> SlotT>
struct [[nodiscard]] result_connection {
    SlotT slot;
    meta::result<ValueT, ErrorT> result;

    void emit() & noexcept {
        if (result.has_value()) {
            slot.set_value(std::move(result).value());
        } else {
            slot.set_error(std::move(result).error());
        }
    }
};

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] result_signal {
    using value_type = ValueT;
    using error_type = ErrorT;

    meta::result<value_type, error_type> result;

    template <Slot<value_type, error_type> SlotT>
    Connection auto subscribe(SlotT&& slot) {
        return result_connection<value_type, error_type, SlotT>{
            .slot = std::move(slot),
            .result = std::move(result),
        };
    }

    executor& get_executor() { return exec::inline_executor(); }
};

} // namespace detail

template <typename ValueT, typename ErrorT = meta::undefined>
constexpr Signal auto as_signal(tl::expected<ValueT, ErrorT> result) {
    return detail::result_signal<ValueT, ErrorT>{ .result = std::move(result) };
}

} // namespace sl::exec
