//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/connection.hpp"
#include "sl/exec/model/executor.hpp"
#include "sl/exec/model/slot.hpp"

#include <sl/meta/traits/unique.hpp>

#include <concepts>
#include <utility>

namespace sl::exec {

template <typename SomeSignalT>
concept SomeSignal =
    requires() {
        typename SomeSignalT::value_type;
        typename SomeSignalT::error_type;
    }
    && requires(
        SomeSignalT signal,
        dummy_slot_ctor<typename SomeSignalT::value_type, typename SomeSignalT::error_type> slot_ctor
    ) {
           { signal.get_executor() } noexcept -> std::same_as<executor&>;
           { std::move(signal).subscribe(std::move(slot_ctor)) } noexcept -> Connection;
       };

template <typename SignalT, typename V, typename E>
concept Signal = SomeSignal<SignalT> //
                 && std::same_as<V, typename SignalT::value_type> //
                 && std::same_as<E, typename SignalT::error_type>;

template <template <typename, typename> typename ForSignalT, SomeSignal SomeSignalT>
using ForSignal = ForSignalT<typename SomeSignalT::value_type, typename SomeSignalT::error_type>;

template <SomeSignal SomeSignalT, typename SlotCtorT>
using ConnectionFor = decltype(std::declval<SomeSignalT&&>().subscribe(std::declval<SlotCtorT&&>()));

template <typename EventT>
concept Event = requires(EventT& event) {
    event.set();
    event.wait();
};

} // namespace sl::exec
