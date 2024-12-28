//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

namespace sl::exec {

template <Signal SignalT, typename SlotT>
struct [[nodiscard]] transform_connection {
    transform_connection(SignalT signal, SlotT slot)
        : slot_{ std::move(slot) }, connection_{ std::move(signal).subscribe(slot_) } {}

    void emit() & { connection_.emit(); }

private:
    SlotT slot_;
    ConnectionFor<SignalT, SlotT> connection_;
};

} // namespace sl::exec
