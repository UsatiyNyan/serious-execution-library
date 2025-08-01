//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/executor.hpp"
#include "sl/exec/model/slot.hpp"

#include <sl/meta/monad/result.hpp>
#include <sl/meta/traits/unique.hpp>

#include <concepts>
#include <utility>

namespace sl::exec {

template <typename ConnectionT>
concept Connection = requires(ConnectionT connection) {
    { connection.get_cancel_handle() } -> std::same_as<cancel_mixin&>;
    std::move(connection).emit();
};

template <typename OrderedT>
concept Ordered = requires(const OrderedT& ordered) {
    { ordered.get_ordering() } -> std::same_as<std::uintptr_t>;
};

template <typename OrderedConnectionT>
concept OrderedConnection = Connection<OrderedConnectionT> && Ordered<OrderedConnectionT>;

template <typename SomeSignalT>
concept SomeSignal = requires() {
    typename SomeSignalT::value_type;
    typename SomeSignalT::error_type;
} && requires(SomeSignalT signal, slot<typename SomeSignalT::value_type, typename SomeSignalT::error_type>& i_slot) {
    { signal.get_executor() } -> std::same_as<executor&>;
    { std::move(signal).subscribe(i_slot) } -> Connection;
};

template <SomeSignal SomeSignalT>
using ISlotFor = slot<typename SomeSignalT::value_type, typename SomeSignalT::error_type>;

template <SomeSignal SomeSignalT>
using ConnectionFor = decltype(std::declval<SomeSignalT&&>().subscribe(std::declval<ISlotFor<SomeSignalT>&>()));

template <typename SignalT, typename ValueT, typename ErrorT>
concept Signal = SomeSignal<SignalT> //
                 && std::same_as<ValueT, typename SignalT::value_type> //
                 && std::same_as<ErrorT, typename SignalT::error_type>;

template <typename EventT>
concept Event = requires(EventT& event) {
    event.set();
    event.wait();
};

} // namespace sl::exec
