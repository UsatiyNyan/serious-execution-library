//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/monad/maybe.hpp>

namespace sl::exec {
namespace detail {

template <typename SignalValueT, typename ValueT, typename ErrorT>
struct [[nodiscard]] flatten_slot : slot<SignalValueT, ErrorT> {
    constexpr explicit flatten_slot(slot<ValueT, ErrorT>& slot) : slot_{ slot } { slot_.intrusive_next = this; }

    void set_value(SignalValueT&& value) & override {
        Connection auto& connection =
            maybe_connection_.emplace(meta::lazy_eval{ [&] { return std::move(value).subscribe(slot_); } });
        std::move(connection).emit();
    }
    void set_error(ErrorT&& error) & override { slot_.set_error(std::move(error)); }
    void set_null() & override { slot_.set_null(); }

private:
    meta::maybe<ConnectionFor<SignalValueT>> maybe_connection_;
    slot<ValueT, ErrorT>& slot_;
};

template <SomeSignal SignalT, SomeSignal SignalValueT = typename SignalT::value_type>
    requires std::same_as<typename SignalT::error_type, typename SignalValueT::error_type>
struct [[nodiscard]] flatten_signal {
    using value_type = typename SignalValueT::value_type;
    using error_type = typename SignalT::error_type;

public:
    constexpr explicit flatten_signal(SignalT signal) : signal_{ std::move(signal) } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        using slot_type = flatten_slot<SignalValueT, value_type, error_type>;
        return subscribe_connection<SignalT, slot_type>{
            /* .signal = */ std::move(signal_),
            /* .slot = */
            [&slot] { return slot_type{ /* .slot = */ slot }; },
        };
    }

    executor& get_executor() { return signal_.get_executor(); }

private:
    SignalT signal_;
};

struct [[nodiscard]] flatten {
    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && {
        return flatten_signal<SignalT>{
            /* .signal = */ std::move(signal),
        };
    }
};

} // namespace detail

constexpr auto flatten() { return detail::flatten{}; }

} // namespace sl::exec
