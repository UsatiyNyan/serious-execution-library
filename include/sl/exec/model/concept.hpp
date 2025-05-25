//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/executor.hpp"

#include <sl/meta/intrusive/forward_list.hpp>
#include <sl/meta/monad/result.hpp>
#include <sl/meta/traits/unique.hpp>
#include <sl/meta/type/undefined.hpp>
#include <sl/meta/type/unit.hpp>

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

template <typename ValueT, typename ErrorT>
struct slot_node
    : slot<ValueT, ErrorT>
    , meta::intrusive_forward_list_node<slot_node<ValueT, ErrorT>> {

    constexpr explicit slot_node(slot<ValueT, ErrorT>& slot) : slot_{ slot } {}

    void set_value(ValueT&& value) & override { slot_.set_value(std::move(value)); }
    void set_error(ErrorT&& error) & override { slot_.set_error(std::move(error)); }
    void cancel() & override { slot_.cancel(); }

private:
    slot<ValueT, ErrorT>& slot_;
};

template <typename ValueT, typename ErrorT>
using slot_list = meta::intrusive_forward_list<slot_node<ValueT, ErrorT>>;

template <typename ConnectionT>
concept Connection = requires(ConnectionT&& connection) { std::move(connection).emit(); };

struct [[nodiscard]] dummy_connection : meta::immovable {
    constexpr void emit() && {}
};

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
