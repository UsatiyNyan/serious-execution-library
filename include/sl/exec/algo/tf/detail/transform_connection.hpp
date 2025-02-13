//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

#include <sl/meta/lifetime/immovable.hpp>

namespace sl::exec {

template <Signal SignalT, typename SlotT>
struct [[nodiscard]] transform_connection : meta::immovable {
    transform_connection(SignalT signal, SlotT slot)
        : slot_{ std::move(slot) }, connection_{ std::move(signal).subscribe(slot_) } {}

    void emit() & { connection_.emit(); }

private:
    SlotT slot_;
    ConnectionFor<SignalT> connection_;
};

} // namespace sl::exec
