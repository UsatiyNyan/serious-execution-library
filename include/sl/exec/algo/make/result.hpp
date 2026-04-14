//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/as_signal.hpp"

#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>
#include <sl/meta/type/unit.hpp>

#include <type_traits>
#include <utility>

namespace sl::exec {
namespace detail {

template <typename V, typename E, typename SlotT>
struct [[nodiscard]] result_connection final {
    meta::maybe<meta::result<V, E>> maybe_result;
    SlotT slot;

    constexpr CancelHandle auto emit() && noexcept {
        fulfill_slot(std::move(slot), std::move(maybe_result));
        return dummy_cancel_handle{};
    }
};

template <typename V, typename E>
struct [[nodiscard]] result_signal final {
    using value_type = V;
    using error_type = E;

    meta::maybe<meta::result<value_type, error_type>> maybe_result;

public:
    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        using SlotT = SlotFrom<SlotCtorT>;
        return result_connection<value_type, error_type, SlotT>{
            .maybe_result = std::move(maybe_result),
            .slot = std::move(slot_ctor)(),
        };
    }

    static executor& get_executor() noexcept { return inline_executor(); }
};

template <typename V, typename E>
struct as_signal<meta::result<V, E>> {
    template <typename ResultTV>
    constexpr static Signal<V, E> auto call(ResultTV&& result) noexcept {
        return result_signal<V, E>{ .maybe_result = std::forward<ResultTV>(result) };
    }
};

template <typename V, typename E>
struct as_signal<meta::maybe<meta::result<V, E>>> {
    template <typename MaybeResultTV>
    constexpr static Signal<V, E> auto call(MaybeResultTV&& result) noexcept {
        return result_signal<V, E>{ .maybe_result = std::forward<MaybeResultTV>(result) };
    }
};

} // namespace detail

template <typename VV>
constexpr SomeSignal auto value_as_signal(VV&& value) {
    return as_signal(meta::ok(std::forward<VV>(value)));
}

template <typename EV>
constexpr SomeSignal auto error_as_signal(EV&& error) {
    using ErrorT = std::decay_t<EV>;
    return as_signal(meta::result<meta::unit, ErrorT>{ std::forward<EV>(error), meta::err_tag });
}

template <typename V = meta::unit, typename E = meta::undefined>
constexpr SomeSignal auto null_as_signal() {
    return as_signal(meta::maybe<meta::result<V, E>>{});
}

} // namespace sl::exec
