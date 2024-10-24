//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, typename EventT>
struct get_slot {
    meta::result<ValueT, ErrorT>& result;
    EventT& event;

    void set_value(ValueT&& value) & {
        result.emplace(meta::ok(std::move(value)));
        event.set();
    }
    void set_error(ErrorT&& error) & {
        result = meta::err(std::move(error));
        event.set();
    }

    void cancel() & { PANIC("unsupported"); }
};

template <Event EventT>
struct get_emit {
    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        using value_type = typename SignalT::value_type;
        using error_type = typename SignalT::error_type;

        meta::result<value_type, error_type> result{};
        EventT event{};

        auto connection = std::move(signal).subscribe(get_slot<value_type, error_type, EventT>{
            .result = result,
            .event = event,
        });

        connection.emit();
        event.wait();

        return result;
    }
};

} // namespace detail

template <Event EventT>
constexpr auto get() {
    return detail::get_emit<EventT>{};
}

} // namespace sl::exec
