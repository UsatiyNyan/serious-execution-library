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
    constexpr result_connection(meta::result<ValueT, ErrorT> result, slot<ValueT, ErrorT>& slot)
        : result_{ std::move(result) }, slot_{ slot } {}

    void emit() && noexcept {
        if (result_.has_value()) {
            slot_.set_value(std::move(result_).value());
        } else {
            slot_.set_error(std::move(result_).error());
        }
    }

private:
    meta::result<ValueT, ErrorT> result_;
    slot<ValueT, ErrorT>& slot_;
};

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] result_signal {
    using value_type = ValueT;
    using error_type = ErrorT;

public:
    explicit constexpr result_signal(meta::result<value_type, error_type> result) : result_{ std::move(result) } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return result_connection<value_type, error_type>{
            /* .result = */ std::move(result_),
            /* .slot = */ slot,
        };
    }

    executor& get_executor() { return exec::inline_executor(); }

private:
    meta::result<value_type, error_type> result_;
};

} // namespace detail

template <typename ValueT, typename ErrorT>
constexpr Signal auto as_signal(meta::result<ValueT, ErrorT> result) {
    return detail::result_signal<ValueT, ErrorT>{ /* .result = */ std::move(result) };
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
