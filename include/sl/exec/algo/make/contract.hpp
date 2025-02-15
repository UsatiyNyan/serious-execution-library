//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/force.hpp"
#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/model/concept.hpp"

#include <sl/meta/lifetime/immovable.hpp>
#include <sl/meta/lifetime/unique.hpp>

#include <libassert/assert.hpp>
#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

struct [[nodiscard]] promise_connection : meta::immovable {
    void emit() & noexcept {}
};

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] promise_signal : meta::unique {
    using value_type = ValueT;
    using error_type = ErrorT;

    explicit promise_signal(slot<ValueT, ErrorT>** slot) : slot_{ slot } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        *slot_ = &slot;
        return promise_connection{};
    }

    executor& get_executor() { return exec::inline_executor(); }

private:
    slot<ValueT, ErrorT>** slot_;
};

} // namespace detail

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] promise
    : slot<ValueT, ErrorT>
    , meta::unique {
    explicit promise(slot<ValueT, ErrorT>* slot) : slot_{ slot } { DEBUG_ASSERT(slot_ != nullptr); }
    ~promise() override {
        if (slot_ != nullptr && !done_) {
            slot_->cancel();
        }
    }
    promise(promise&& other) noexcept
        : slot_{ std::exchange(other.slot_, nullptr) }, done_{ std::exchange(other.done_, false) } {}

    void set_value(ValueT&& value) & override {
        if (ASSUME_VAL(!done_)) {
            slot_->set_value(std::move(value));
            done_ = true;
        }
    }
    void set_error(ErrorT&& error) & override {
        if (ASSUME_VAL(!done_)) {
            slot_->set_error(std::move(error));
            done_ = true;
        }
    }
    void cancel() & override {
        if (ASSUME_VAL(!done_)) {
            slot_->cancel();
            done_ = true;
        }
    }

private:
    slot<ValueT, ErrorT>* slot_;
    bool done_ = false;
};

template <typename ValueT, typename ErrorT>
struct contract {
    using future_type = detail::force_signal<detail::promise_signal<ValueT, ErrorT>>;
    using promise_type = promise<ValueT, ErrorT>;

    future_type future;
    promise_type promise;
};

template <typename ValueT, typename ErrorT>
contract<ValueT, ErrorT> make_contract() {
    slot<ValueT, ErrorT>* promise_slot = nullptr;
    detail::force_signal<detail::promise_signal<ValueT, ErrorT>> signal =
        force()(detail::promise_signal<ValueT, ErrorT>{ &promise_slot });
    return contract<ValueT, ErrorT>{
        .future = std::move(signal),
        .promise{ promise_slot },
    };
}

} // namespace sl::exec
