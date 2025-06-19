//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"
#include "sl/exec/model/concept.hpp"

namespace sl::exec {
namespace detail {

template <SomeSignal SignalT>
struct [[nodiscard]] detach_connection {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

    struct detach_slot final : slot<value_type, error_type> {
        explicit detach_slot(detach_connection* self) : self_{ self } {}

        void set_value(value_type&&) & override { delete self_; }
        void set_error(error_type&&) & override { delete self_; }
        void set_null() & override { delete self_; }

    private:
        detach_connection* self_;
    };

public:
    constexpr detach_connection(SignalT signal) : connection_{ std::move(signal), detach_slot{ this } } {}

    cancel_mixin& get_cancel_handle() & { return connection_.get_cancel_handle(); }
    void emit() && { std::move(connection_).emit(); }

private:
    subscribe_connection<SignalT, detach_slot> connection_;
};

struct detach_emit {
    template <SomeSignal SignalT>
    constexpr void operator()(SignalT&& signal) && {
        auto& connection = *(new detach_connection<SignalT>{ std::move(signal) });
        std::move(connection).emit();
    }
};

} // namespace detail

constexpr auto detach() { return detail::detach_emit{}; }

} // namespace sl::exec
