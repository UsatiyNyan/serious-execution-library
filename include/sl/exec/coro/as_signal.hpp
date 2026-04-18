//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/as_signal.hpp"
#include "sl/exec/coro/async.hpp"
#include "sl/exec/model/concept.hpp"

#include <sl/meta/lifetime/defer.hpp>

#include <exception>

namespace sl::exec::detail {

template <typename ValueT, typename ErrorT, typename SlotT>
struct [[nodiscard]] async_connection final {
public:
    constexpr async_connection(async<ValueT> an_async, SlotT slot, executor& executor)
        : async_{ std::move(an_async) }, slot_{ std::move(slot) }, executor_{ executor } {}

    CancelHandle auto emit() && noexcept {
        coro_schedule(executor_, make_connection_coro(std::move(async_), std::move(slot_)));
        return dummy_cancel_handle{};
    }

private:
    static async<void> make_connection_coro(async<ValueT> an_async, SlotT slot) {
        // coroutine handle can be destroyed at any point, when executor destroys it's tasks
        bool is_fulfilled = false;
        const meta::defer set_null_if_not_fulfilled{ [&] {
            if (!is_fulfilled) {
                std::move(slot).set_null();
            }
        } };
        try {
            std::move(slot).set_value(co_await std::move(an_async));
        } catch (...) {
            std::move(slot).set_error(std::current_exception());
        }
        is_fulfilled = true;
    }

private:
    async<ValueT> async_;
    SlotT slot_;
    executor& executor_;
};

template <typename T>
struct [[nodiscard]] async_signal final {
    using value_type = T;
    using error_type = std::exception_ptr;

public:
    constexpr explicit async_signal(async<T> an_async) : async_{ std::move(an_async) } {}

    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        using SlotT = SlotFrom<SlotCtorT>;
        return async_connection<value_type, error_type, SlotT>{
            std::move(async_),
            std::move(slot_ctor)(),
            get_executor(),
        };
    }

    static executor& get_executor() noexcept { return exec::inline_executor(); }

private:
    async<T> async_;
};

template <typename T>
struct as_signal<async<T>> {
    constexpr static Signal<T, std::exception_ptr> auto call(async<T> an_async) noexcept {
        return async_signal<T>{ std::move(an_async) };
    }
};

} // namespace sl::exec::detail
