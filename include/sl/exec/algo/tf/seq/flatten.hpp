//
// Created by usatiynyan.
// `flatten()` breaks cancellation chain for a signal created dynamically
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
struct [[nodiscard]] flatten_slot final : slot<SignalValueT, ErrorT> {
    constexpr explicit flatten_slot(slot<ValueT, ErrorT>& slot) : slot_{ slot } {}

    void set_value(SignalValueT&& value) & override {
        connection& a_connection =
            maybe_connection_.emplace(meta::lazy_eval{ [&] { return std::move(value).subscribe(slot_); } });
        // need to ignore cancel_handle here, otherwise - race
        std::ignore = std::move(a_connection).emit();
    }
    void set_error(ErrorT&& error) & override { slot_.set_error(std::move(error)); }
    void set_null() & override { slot_.set_null(); }

private:
    meta::maybe<ConnectionFor<SignalValueT>> maybe_connection_;
    slot<ValueT, ErrorT>& slot_;
};

template <SomeSignal SignalT, SomeSignal SignalValueT = typename SignalT::value_type>
    requires std::same_as<typename SignalT::error_type, typename SignalValueT::error_type>
struct [[nodiscard]] flatten_signal final {
    using value_type = typename SignalValueT::value_type;
    using error_type = typename SignalT::error_type;
    using slot_type = flatten_slot<SignalValueT, value_type, error_type>;

public:
    constexpr explicit flatten_signal(SignalT&& signal) : signal_{ std::move(signal) } {}

    subscribe_connection<SignalT, slot_type> subscribe(slot<value_type, error_type>& slot) && {
        return subscribe_connection<SignalT, slot_type>{
            std::move(signal_),
            [&slot] { return slot_type{ slot }; },
        };
    }

    executor& get_executor() { return signal_.get_executor(); }

private:
    SignalT signal_;
};

struct [[nodiscard]] flatten final {
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
