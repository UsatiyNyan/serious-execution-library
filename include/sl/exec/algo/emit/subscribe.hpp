//
// Created by usatiynyan.
//
// for long lived "connections" and manual emits
//
// Example:
// subscribe_connection imalive = signal | subscribe();
// imalive.emit();
//

#pragma once

#include "sl/exec/model/concept.hpp"

#include <sl/meta/lifetime/immovable.hpp>

namespace sl::exec {

template <Signal SignalT>
class [[nodiscard]] subscribe_connection : meta::immovable {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

public:
    explicit subscribe_connection(SignalT&& signal) : connection_{ std::move(signal).subscribe(slot_) } {}

    void emit() & { connection_.emit(); }

private:
    dummy_slot<value_type, error_type> slot_{};
    ConnectionFor<SignalT> connection_;
};

namespace detail {

struct subscribe_emit {
    template <Signal SignalT>
    constexpr Connection auto operator()(SignalT&& signal) && {
        return subscribe_connection<SignalT>{ std::move(signal) };
    }
};

} // namespace detail

constexpr auto subscribe() { return detail::subscribe_emit{}; }

} // namespace sl::exec
