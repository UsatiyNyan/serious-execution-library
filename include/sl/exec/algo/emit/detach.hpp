//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

namespace sl::exec {
namespace detail {

template <SomeSignal SignalT>
struct [[nodiscard]] detach_connection final {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

    struct detach_slot final {
        detach_connection* self;

        constexpr void set_value(value_type&&) && noexcept { delete self; }
        constexpr void set_error(error_type&&) && noexcept { delete self; }
        constexpr void set_null() && noexcept { delete self; }
    };

    struct detach_slot_ctor final {
        detach_connection* self;

        constexpr auto operator()() && noexcept { return detach_slot{ self }; }
    };

public:
    constexpr detach_connection(SignalT signal)
        : connection_{ std::move(signal).subscribe(detach_slot_ctor{ this }) } {}

    CancelHandle auto emit() && noexcept { return std::move(connection_).emit(); }

private:
    ConnectionFor<SignalT, detach_slot_ctor> connection_;
};

struct detach_emit {
    template <SomeSignal SignalT>
    constexpr void operator()(SignalT&& signal) && {
        auto& connection = *(new detach_connection<SignalT>{ std::move(signal) });
        std::ignore = std::move(connection).emit();
    }
};

} // namespace detail

constexpr auto detach() { return detail::detach_emit{}; }

} // namespace sl::exec
