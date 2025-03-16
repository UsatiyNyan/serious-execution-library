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

template <Signal SignalT, typename SlotT>
class [[nodiscard]] subscribe_connection : meta::immovable {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

public:
    constexpr subscribe_connection(SignalT signal, SlotT slot)
        : slot_{ std::move(slot) }, connection_{ std::move(signal).subscribe(slot_) } {}

    void emit() && { std::move(connection_).emit(); }

private:
    SlotT slot_{};
    ConnectionFor<SignalT> connection_;
};

namespace detail {

struct subscribe_emit {
    template <Signal SignalT>
    constexpr Connection auto operator()(SignalT&& signal) && {
        using value_type = typename SignalT::value_type;
        using error_type = typename SignalT::error_type;
        return subscribe_connection{
            /* .signal = */ std::move(signal),
            /* .slot = */ dummy_slot<value_type, error_type>{},
        };
    }
};

} // namespace detail

constexpr auto subscribe() { return detail::subscribe_emit{}; }

} // namespace sl::exec
