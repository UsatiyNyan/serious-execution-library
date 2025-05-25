//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/as_signal.hpp"
#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/coro/async.hpp"
#include "sl/exec/model/concept.hpp"

#include <sl/meta/lifetime/defer.hpp>

#include <exception>

namespace sl::exec::detail {

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] async_connection {
public:
    constexpr async_connection(async<ValueT> async, slot<ValueT, ErrorT>& slot, executor& executor)
        : async_{ std::move(async) }, slot_{ slot }, executor_{ executor } {}

    void emit() && noexcept { coro_schedule(executor_, make_connection_coro(std::move(async_), slot_)); }

private:
    static async<void> make_connection_coro(async<ValueT> async, slot<ValueT, ErrorT>& slot) {
        // coroutine handle can be destroyed at any point, when executor destroys it's tasks
        bool is_fulfilled = false;
        const meta::defer cancel_if_not_fulfilled{ [&] {
            if (!is_fulfilled) {
                slot.cancel();
            }
        } };
        try {
            slot.set_value(co_await std::move(async));
        } catch (...) {
            slot.set_error(std::current_exception());
        }
        is_fulfilled = true;
    }

private:
    async<ValueT> async_;
    slot<ValueT, ErrorT>& slot_;
    executor& executor_;
};

template <typename T>
struct [[nodiscard]] async_signal {
    using value_type = T;
    using error_type = std::exception_ptr;

public:
    constexpr explicit async_signal(async<T> async) : async_{ std::move(async) } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return async_connection<value_type, error_type>{
            /* .result = */ std::move(async_),
            /* .slot = */ slot,
            /* .executor = */ get_executor(),
        };
    }

    executor& get_executor() { return exec::inline_executor(); }

private:
    async<T> async_;
};

template <typename T>
struct as_signal<async<T>> {
    constexpr static Signal<T, std::exception_ptr> auto call(async<T> async) {
        return async_signal<T>{ /* .async = */ std::move(async) };
    }
};

} // namespace sl::exec::detail
