//
// Created by usatiynyan.
// TODO: context
//

#pragma once

#include <tl/expected.hpp>

namespace sl::exec {

template <typename V, typename E>
using result = tl::expected<V, E>;

template <typename SlotT, typename ResultT>
concept SlotFromResult = requires(SlotT slot, ResultT result) {
    slot.set_result(result);
    slot.cancel();
};

template <typename SlotT, typename V, typename E>
concept Slot = SlotFromResult<SlotT, result<V, E>>;

template <typename SignalT>
concept Signal = requires(SignalT signal) { typename SignalT::result_type; };

template <typename SignalT, typename SlotT>
concept SignalTo = //
    Signal<SignalT> && //
    SlotFromResult<SlotT, typename SignalT::result_type> && //
    requires(SignalT& signal, SlotT slot) { signal.connect(slot); };

template <typename SignalT, typename SlotTV>
    requires SignalTo<SignalT, std::decay_t<SlotTV>>
void connect(SignalT& signal, SlotTV&& slot) {
    signal.connect(std::forward<SlotTV>(slot));
}

} // namespace sl::exec
