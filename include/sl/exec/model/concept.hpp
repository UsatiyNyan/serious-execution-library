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

template <typename ValueT, typename ErrorT>
struct slot {
    virtual ~slot() = default;

    virtual void set_value(ValueT&&) & = 0;
    virtual void set_error(ErrorT&&) & = 0;
    virtual void cancel() & = 0;
};

template <typename ValueT, typename ErrorT>
struct dummy_slot final : slot<ValueT, ErrorT> {
    explicit dummy_slot() = default;

    void set_value(ValueT&&) & override {}
    void set_error(ErrorT&&) & override {}
    void cancel() & override {}
};

template <typename ConnectionT>
concept Connection = requires(ConnectionT&& connection) { std::move(connection).emit(); };

template <typename SignalT>
concept Signal = requires(
    SignalT& l_signal,
    SignalT&& r_signal,
    slot<typename SignalT::value_type, typename SignalT::error_type>& i_slot
) {
    typename SignalT::value_type;
    typename SignalT::error_type;
    { l_signal.get_executor() } -> std::same_as<executor&>;
    { std::move(r_signal).subscribe(i_slot) } -> Connection;
};

template <typename EventT>
concept Event = requires(EventT& event) {
    event.set();
    event.wait();
};

template <Signal SignalT>
using ISlotFor = slot<typename SignalT::value_type, typename SignalT::error_type>;

template <Signal SignalT>
using ConnectionFor = decltype(std::declval<SignalT&&>().subscribe(std::declval<ISlotFor<SignalT>&>()));

} // namespace sl::exec
