//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/force.hpp"
#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/syntax.hpp"

#include <sl/meta/lifetime/finalizer.hpp>
#include <sl/meta/traits/unique.hpp>

#include <libassert/assert.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] promise_connection : meta::immovable {
    explicit promise_connection(slot<ValueT, ErrorT>& slot) : slot_{ slot } {}

    cancel_mixin& get_cancel_handle() & { return slot_; }

    constexpr void emit() && {}

private:
    slot<ValueT, ErrorT>& slot_;
};

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] promise_signal : meta::unique {
    using value_type = ValueT;
    using error_type = ErrorT;

public:
    explicit promise_signal(slot<ValueT, ErrorT>** slot) : slot_{ slot } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        *slot_ = &slot;
        return promise_connection{ slot };
    }

    executor& get_executor() { return exec::inline_executor(); }

private:
    slot<ValueT, ErrorT>** slot_;
};

} // namespace detail

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] promise : meta::finalizer<promise<ValueT, ErrorT>> {
    explicit promise(slot<ValueT, ErrorT>* slot)
        : meta::finalizer<promise>{ [](promise& self) {
              if (auto* const slot = std::exchange(self.slot_, nullptr); slot != nullptr) {
                  slot->set_null();
              }
          } },
          slot_{ slot } {
        DEBUG_ASSERT(slot_ != nullptr);
    }

    void set_value(ValueT&& value) & {
        auto* const slot = std::exchange(slot_, nullptr);
        if (ASSUME_VAL(slot != nullptr)) {
            slot->set_value(std::move(value));
        }
    }
    void set_error(ErrorT&& error) & {
        auto* const slot = std::exchange(slot_, nullptr);
        if (ASSUME_VAL(slot != nullptr)) {
            slot->set_error(std::move(error));
        }
    }
    void set_null() & {
        auto* const slot = std::exchange(slot_, nullptr);
        if (ASSUME_VAL(slot != nullptr)) {
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

    future_type future;
    promise_type promise;
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
contract<ValueT, ErrorT, Atomic> make_contract() {
    slot<ValueT, ErrorT>* promise_slot = nullptr;
    auto signal = detail::promise_signal<ValueT, ErrorT>{ &promise_slot } | force();
    return contract<ValueT, ErrorT, Atomic>{
        .future = std::move(signal),
        .promise{ promise_slot },
    };
}

} // namespace sl::exec
