//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/model/concept.hpp"

#include <tl/expected.hpp>
#include <type_traits>
#include <utility>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] result_connection {
    meta::result<ValueT, ErrorT> result;
    slot<ValueT, ErrorT>& slot;

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

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return result_connection<value_type, error_type>{
            .result = std::move(result),
            .slot = slot,
        };
    }

    executor& get_executor() { return exec::inline_executor(); }
};

} // namespace detail

template <typename ValueT, typename ErrorT>
constexpr Signal auto as_signal(meta::result<ValueT, ErrorT> result) {
    return detail::result_signal<ValueT, ErrorT>{ .result = std::move(result) };
}

template <typename ValueTV>
constexpr Signal auto value_as_signal(ValueTV&& value) {
    using ValueT = std::decay_t<ValueTV>;
    return as_signal(meta::result<ValueT, meta::undefined>{ tl::in_place, std::forward<ValueTV>(value) });
}

template <typename ErrorTV>
constexpr Signal auto error_as_signal(ErrorTV&& error) {
    using ErrorT = std::decay_t<ErrorTV>;
    return as_signal(meta::result<meta::unit, ErrorT>{ tl::unexpect, std::forward<ErrorTV>(error) });
}

} // namespace sl::exec
