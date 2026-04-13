//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>
#include <sl/meta/traits/unique.hpp>

namespace sl::exec {

template <typename SlotT, typename V, typename E>
concept Slot = requires(SlotT& slot, V&& value, E&& error) {
    { std::move(slot).set_value(std::move(value)) } noexcept;
    { std::move(slot).set_error(std::move(error)) } noexcept;
    { std::move(slot).set_null() } noexcept;
};

template <typename V, typename E>
struct slot_callback {
    virtual ~slot_callback() = default;
    virtual void set_result(meta::maybe<meta::result<V, E>>&& maybe_result) && noexcept = 0;
};

template <typename SlotForT, typename SomeSignalT>
concept SlotFor = Slot<SlotForT, typename SomeSignalT::value_type, typename SomeSignalT::error_type>;

template <typename V, typename E>
struct dummy_slot final {
    constexpr void set_value(V&&) && noexcept {}
    constexpr void set_error(E&&) && noexcept {}
    constexpr void set_null() && noexcept {}
};

template <typename SlotCtorT, typename V, typename E>
concept SlotCtor = requires(SlotCtorT slot_ctor) {
    { std::move(slot_ctor)() } noexcept -> Slot<V, E>;
};

template <typename SlotCtorForT, typename SomeSignalT>
concept SlotCtorFor = SlotCtor<SlotCtorForT, typename SomeSignalT::value_type, typename SomeSignalT::error_type>;

template <typename SlotCtorT>
using SlotFrom = decltype(std::declval<SlotCtorT&&>()());

template <typename V, typename E>
struct dummy_slot_ctor final {
    constexpr Slot<V, E> auto operator()() && noexcept { return dummy_slot<V, E>{}; }
};

template <typename V, typename E, Slot<V, E> SlotT>
void fulfill_slot(SlotT&& slot, meta::maybe<meta::result<V, E>> maybe_result) noexcept {
    if (!maybe_result.has_value()) {
        std::move(slot).set_null();
        return;
    }

    meta::result<V, E> result = std::move(maybe_result).value();
    if (result.has_value()) {
        std::move(slot).set_value(std::move(result).value());
    } else {
        std::move(slot).set_error(std::move(result).error());
    }
}

} // namespace sl::exec
