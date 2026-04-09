//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/as_signal.hpp"
#include "sl/exec/model/connection.hpp"
#include "sl/exec/model/syntax.hpp"

#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>
#include <sl/meta/type/unit.hpp>

#include <type_traits>
#include <utility>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] result_connection final : connection {
    constexpr result_connection(meta::maybe<meta::result<ValueT, ErrorT>> maybe_result, slot<ValueT, ErrorT>& slot)
        : maybe_result_{ std::move(maybe_result) }, slot_{ slot } {}

    cancel_handle& emit() && noexcept override {
        fulfill_slot(slot_, std::move(maybe_result_));
        return dummy_cancel_handle();
    }

private:
    meta::maybe<meta::result<ValueT, ErrorT>> maybe_result_;
    slot<ValueT, ErrorT>& slot_;
};

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] result_signal final {
    using value_type = ValueT;
    using error_type = ErrorT;

public:
    constexpr explicit result_signal(meta::maybe<meta::result<value_type, error_type>> maybe_result)
        : maybe_result_{ std::move(maybe_result) } {}

    result_connection<value_type, error_type> subscribe(slot<value_type, error_type>& slot) && {
        return result_connection<value_type, error_type>{ std::move(maybe_result_), slot };
    }

    executor& get_executor() { return inline_executor(); }

private:
    meta::maybe<meta::result<value_type, error_type>> maybe_result_;
};

template <typename ValueT, typename ErrorT>
struct as_signal<meta::result<ValueT, ErrorT>> {
    template <typename ResultTV>
    constexpr static Signal<ValueT, ErrorT> auto call(ResultTV&& result) {
        return result_signal<ValueT, ErrorT>{ std::forward<ResultTV>(result) };
    }
};

template <typename ValueT, typename ErrorT>
struct as_signal<meta::maybe<meta::result<ValueT, ErrorT>>> {
    template <typename MaybeResultTV>
    constexpr static Signal<ValueT, ErrorT> auto call(MaybeResultTV&& result) {
        return result_signal<ValueT, ErrorT>{ std::forward<MaybeResultTV>(result) };
    }
};

} // namespace detail

template <typename ValueTV>
constexpr SomeSignal auto value_as_signal(ValueTV&& value) {
    return as_signal(meta::ok(std::forward<ValueTV>(value)));
}

template <typename ErrorTV>
constexpr SomeSignal auto error_as_signal(ErrorTV&& error) {
    using ErrorT = std::decay_t<ErrorTV>;
    return as_signal(meta::result<meta::unit, ErrorT>{ std::forward<ErrorTV>(error), meta::err_tag });
}

template <typename ValueT = meta::unit, typename ErrorT = meta::undefined>
constexpr SomeSignal auto null_as_signal() {
    return as_signal(meta::maybe<meta::result<ValueT, ErrorT>>{});
}

} // namespace sl::exec
