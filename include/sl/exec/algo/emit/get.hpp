//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/event.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/monad/maybe.hpp>

namespace sl::exec {
namespace detail {

template <typename V, typename E, typename EventT>
struct get_slot final {
    meta::maybe<meta::result<V, E>>& maybe_result;
    EventT& event;

public:
    void set_value(V&& value) && noexcept {
        maybe_result.emplace(meta::ok_tag, std::move(value));
        event.set();
    }
    void set_error(E&& error) && noexcept {
        maybe_result.emplace(meta::err_tag, std::move(error));
        event.set();
    }
    void set_null() && noexcept { event.set(); }
};

template <Event EventT>
struct [[nodiscard]] get_emit {
    template <SomeSignal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        using value_type = typename SignalT::value_type;
        using error_type = typename SignalT::error_type;

        meta::maybe<meta::result<value_type, error_type>> maybe_result{};
        EventT event{};

        auto connection = std::move(signal).subscribe(
            [&]() noexcept { return get_slot<value_type, error_type, EventT>{ maybe_result, event }; }
        );
        std::move(connection).emit();

        event.wait();
        return maybe_result;
    }
};

} // namespace detail

template <Event EventT = default_event>
constexpr auto get() {
    return detail::get_emit<EventT>{};
}

} // namespace sl::exec
