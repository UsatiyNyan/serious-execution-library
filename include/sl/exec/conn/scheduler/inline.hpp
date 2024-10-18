//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/conn/concept.hpp"

#include <sl/meta/func/unit.hpp>

namespace sl::exec {

struct [[nodiscard]] inline_scheduler {
    template <typename SlotT>
    struct [[nodiscard]] connection {
        SlotT slot;

        void emit() && { std::move(slot).set_result(sl::meta::unit{}); }
    };

    struct [[nodiscard]] signal {
        using result_type = sl::meta::unit;

        template <Slot<result_type> SlotT>
        Connection auto connect(SlotT slot) && {
            return connection<SlotT>{ .slot = std::move(slot) };
        }
    };

    Signal auto schedule() { return signal{}; }
};

} // namespace sl::exec
