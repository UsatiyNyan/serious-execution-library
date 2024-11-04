//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/event.hpp"
#include "sl/exec/model/concept.hpp"

#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, typename EventT>
struct get_slot final : slot<ValueT, ErrorT> {
    using result_type = meta::result<ValueT, ErrorT>;

    void set_value(ValueT&& value) & override {
        maybe_result.emplace(result_type{ meta::ok(std::move(value)) });
        event.set();
    }
    void set_error(ErrorT&& error) & override {
        maybe_result.emplace(result_type{ meta::err(std::move(error)) });
        event.set();
    }

    void cancel() & override { ASSUME(!maybe_result.has_value()); }

    tl::optional<result_type> maybe_result{};
    EventT event{};
};

template <Event EventT>
struct [[nodiscard]] get_emit {
    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        using value_type = typename SignalT::value_type;
        using error_type = typename SignalT::error_type;

        get_slot<value_type, error_type, EventT> slot;
        auto connection = std::move(signal).subscribe(slot);

        connection.emit();
        slot.event.wait();

        return slot.maybe_result;
    }
};

} // namespace detail

template <Event EventT = default_event>
constexpr auto get() {
    return detail::get_emit<EventT>{};
}

} // namespace sl::exec
