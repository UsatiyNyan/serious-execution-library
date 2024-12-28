//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/force.hpp"
#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/model/concept.hpp"

#include <sl/meta/lifetime/immovable.hpp>

#include <libassert/assert.hpp>
#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

struct [[nodiscard]] promise_connection {
    void emit() & {}
};

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] promise_signal {
    using value_type = ValueT;
    using error_type = ErrorT;

    explicit promise_signal(tl::optional<slot<ValueT, ErrorT>&>& maybe_promise_slot)
        : maybe_promise_slot_{ maybe_promise_slot } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        maybe_promise_slot_.emplace(slot);
        return promise_connection{};
    }

    executor& get_executor() { return exec::inline_executor(); }

private:
    tl::optional<slot<ValueT, ErrorT>&>& maybe_promise_slot_;
};

} // namespace detail

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] promise
    : slot<ValueT, ErrorT>
    , meta::immovable {
    explicit promise(slot<ValueT, ErrorT>& slot) : slot_{ slot } {}
    ~promise() override {
        if (!done_) {
            slot_.cancel();
        }
    }

    void set_value(ValueT&& value) & override {
        if (ASSUME_VAL(!done_)) {
            slot_.set_value(std::move(value));
            done_ = true;
        }
    }
    void set_error(ErrorT&& error) & override {
        if (ASSUME_VAL(!done_)) {
            slot_.set_error(std::move(error));
            done_ = true;
        }
    }
    void cancel() & override {
        if (ASSUME_VAL(!done_)) {
            slot_.cancel();
            done_ = true;
        }
    }

private:
    slot<ValueT, ErrorT>& slot_;
    bool done_ = false;
};

template <typename ValueT, typename ErrorT>
std::tuple<detail::force_signal<detail::promise_signal<ValueT, ErrorT>>, promise<ValueT, ErrorT>> make_contract() {
    tl::optional<slot<ValueT, ErrorT>&> maybe_promise_slot;
    auto signal = force()(detail::promise_signal<ValueT, ErrorT>{ maybe_promise_slot });
    DEBUG_ASSERT(maybe_promise_slot.has_value());
    return std::make_tuple(std::move(signal), promise<ValueT, ErrorT>{ maybe_promise_slot.value() });
}

} // namespace sl::exec
