//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

namespace sl::exec {
namespace detail {

template <Signal SignalT>
class detach_connection {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

    struct detach_slot final : slot<value_type, error_type> {
        explicit detach_slot(detach_connection* self) : self_{ self } {}

        void set_value(value_type&&) & override { delete self_; }
        void set_error(error_type&&) & override { delete self_; }
        void cancel() & override { delete self_; }

    private:
        detach_connection* self_;
    };

public:
    detach_connection(SignalT&& signal) : slot_{ this }, connection_{ std::move(signal).subscribe(slot_) } {}

    void emit() & { connection_.emit(); }

private:
    detach_slot slot_;
    ConnectionFor<SignalT, detach_slot> connection_;
};

struct detach_emit {
    template <Signal SignalT>
    constexpr void operator()(SignalT&& signal) && {
        (new detach_connection<SignalT>{ std::move(signal) })->emit();
    }
};

} // namespace detail

constexpr auto detach() { return detail::detach_emit{}; }

} // namespace sl::exec
