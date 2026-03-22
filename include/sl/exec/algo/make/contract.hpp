//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/force.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/lifetime/finalizer.hpp>
#include <sl/meta/traits/unique.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] promise_signal final : meta::unique {
    using value_type = ValueT;
    using error_type = ErrorT;

public:
    explicit promise_signal(slot<ValueT, ErrorT>** a_slot) : slot_{ a_slot } {}

    dummy_connection subscribe(slot<value_type, error_type>& slot) && {
        *slot_ = &slot;
        return dummy_connection{};
    }

    executor& get_executor() { return inline_executor(); }

private:
    slot<ValueT, ErrorT>** slot_;
};

} // namespace detail

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] promise : meta::finalizer<promise<ValueT, ErrorT>> {
    explicit promise(slot<ValueT, ErrorT>* a_slot) : meta::finalizer<promise>{ finalize }, slot_{ a_slot } {
        DEBUG_ASSERT(slot_ != nullptr);
    }

    static void finalize(promise& self) {
        if (auto* const a_slot = std::exchange(self.slot_, nullptr); a_slot != nullptr) {
            a_slot->set_null();
        }
    }

    void set_value(ValueT&& value) & {
        auto* const slot = std::exchange(slot_, nullptr);
        if (ASSERT_VAL(slot != nullptr)) {
            slot->set_value(std::move(value));
        }
    }
    void set_error(ErrorT&& error) & {
        auto* const slot = std::exchange(slot_, nullptr);
        if (ASSERT_VAL(slot != nullptr)) {
            slot->set_error(std::move(error));
        }
    }
    void set_null() & {
        auto* const slot = std::exchange(slot_, nullptr);
        if (ASSERT_VAL(slot != nullptr)) {
            slot->set_null();
        }
    }

private:
    slot<ValueT, ErrorT>* slot_;
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
struct contract {
    using future_type = detail::force_signal<detail::promise_signal<ValueT, ErrorT>, Atomic>;
    using promise_type = promise<ValueT, ErrorT>;

    future_type f;
    promise_type p;
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
contract<ValueT, ErrorT, Atomic> make_contract() {
    slot<ValueT, ErrorT>* promise_slot = nullptr;
    auto signal = force()(detail::promise_signal<ValueT, ErrorT>{ &promise_slot });
    return contract<ValueT, ErrorT, Atomic>{
        .f = std::move(signal),
        .p{ promise_slot },
    };
}

} // namespace sl::exec
