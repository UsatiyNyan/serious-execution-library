//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/monad/maybe.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, typename EventT>
struct get_slot final : slot<ValueT, ErrorT> {
    using result_type = meta::result<ValueT, ErrorT>;

public:
    void set_value(ValueT&& value) & override {
        maybe_result.emplace(tl::in_place, std::move(value));
        event.set();
    }
    void set_error(ErrorT&& error) & override {
        maybe_result.emplace(tl::unexpect, std::move(error));
        event.set();
    }

    void set_null() & override {
        ASSERT(!maybe_result.has_value());
        event.set();
    }

public:
    meta::maybe<result_type> maybe_result{};
    EventT event{};
};

template <Event EventT>
struct [[nodiscard]] get_emit {
    template <SomeSignal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        using value_type = typename SignalT::value_type;
        using error_type = typename SignalT::error_type;

        get_slot<value_type, error_type, EventT> slot;
        auto connection = std::move(signal).subscribe(slot);

        std::move(connection).emit();
        slot.event.wait();

        return slot.maybe_result;
    }
};

} // namespace detail

template <Event EventT>
constexpr auto get() {
    return detail::get_emit<EventT>{};
}

} // namespace sl::exec
