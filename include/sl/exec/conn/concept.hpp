//
// Created by usatiynyan.
// TODO: context
//

#pragma once

#include <utility>

namespace sl::exec {

template <typename SlotT, typename ResultT>
concept Slot = requires(SlotT slot, ResultT&& result) {
    std::move(slot).set_result(std::move(result));
    std::move(slot).cancel();
};

template <typename SignalT>
concept Signal = requires(SignalT signal) { typename SignalT::result_type; };

template <typename ConnectionT>
concept Connection = requires(ConnectionT&& connection) { std::move(connection).emit(); };

template <typename SignalT, typename SlotT>
concept SignalTo = //
    Signal<SignalT> && //
    Slot<SlotT, typename SignalT::result_type> && //
    requires(SignalT&& signal, SlotT&& slot) {
        { std::move(signal).connect(std::move(slot)) } -> Connection;
    };

template <typename SchedulerT>
concept Scheduler = requires(SchedulerT& scheduler) {
    { scheduler.schedule() } -> Signal;
};

template <typename SignalT, typename SlotT>
    requires SignalTo<SignalT, SlotT>
Connection auto connect(SignalT&& signal, SlotT&& slot) {
    return std::move(signal).connect(std::move(slot));
}

} // namespace sl::exec
