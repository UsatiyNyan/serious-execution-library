//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/executor.hpp"

#include <sl/meta/func/undefined.hpp>
#include <sl/meta/func/unit.hpp>
#include <sl/meta/monad/result.hpp>

#include <concepts>
#include <utility>

namespace sl::exec {

template <typename EventT>
concept Event = requires(EventT& event) {
    event.set();
    event.wait();
};

template <typename SlotT, typename ValueT, typename ErrorT>
concept Slot = requires(SlotT& slot, ValueT&& value, ErrorT&& error) {
    slot.set_value(std::move(value));
    slot.set_error(std::move(error));
    slot.cancel(); // TODO: maybe continuation also
};

template <typename SignalT>
concept Signal = requires(SignalT signal) {
    typename SignalT::value_type;
    typename SignalT::error_type;
    { signal.get_executor() } -> std::same_as<executor&>;
};

template <typename ConnectionT>
concept Connection = requires(ConnectionT& connection) { connection.emit(); };

template <typename SignalT, typename SlotT>
concept SignalTo = //
    Signal<SignalT> && //
    Slot<SlotT, typename SignalT::value_type, typename SignalT::error_type> && //
    requires(SignalT& signal, SlotT&& slot) {
        { signal.subscribe(std::move(slot)) } -> Connection;
    };

template <typename SchedulerT>
concept Scheduler = requires(SchedulerT& scheduler) {
    { scheduler.schedule() } -> Signal;
};

template <typename SignalT, typename SlotT>
    requires SignalTo<SignalT, SlotT>
constexpr Connection auto subscribe(SignalT& signal, SlotT&& slot) {
    return signal.subscribe(std::move(slot));
}

} // namespace sl::exec
