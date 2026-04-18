//
// Created by usatiynyan.
// Expected to use with coro_schedule.
// If coroutine is handled manually - make sure not to destroy it's frame before the awaited signal finishes executing.
//

#pragma once

#include "sl/exec/model/concept.hpp"

#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>

#include <coroutine>
#include <variant>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT>
struct awaiter_slot final {
    struct awaiter_task final : task_node {
        explicit awaiter_task(std::coroutine_handle<> handle) : handle_{ std::move(handle) } {}

        void execute() noexcept override { handle_.resume(); }
        void cancel() noexcept override { handle_.destroy(); }

    private:
        std::coroutine_handle<> handle_;
    };

    awaiter_task task_;
    meta::maybe<meta::result<ValueT, ErrorT>>& maybe_result_;
    executor& executor_;

    void set_value(ValueT&& value) && noexcept {
        maybe_result_.emplace(tl::in_place, std::move(value));
        executor_.schedule(task_);
    }
    void set_error(ErrorT&& error) && noexcept {
        maybe_result_.emplace(tl::unexpect, std::move(error));
        executor_.schedule(task_);
    }
    void set_null() && noexcept { task_.cancel(); }
};

template <typename ValueT, typename ErrorT>
struct awaiter_slot_ctor final {
    std::coroutine_handle<> handle;
    meta::maybe<meta::result<ValueT, ErrorT>>& maybe_result;
    executor& ex;

    constexpr awaiter_slot<ValueT, ErrorT> operator()() && noexcept {
        return awaiter_slot<ValueT, ErrorT>{
            .task_ = typename awaiter_slot<ValueT, ErrorT>::awaiter_task{ std::move(handle) },
            .maybe_result_ = maybe_result,
            .executor_ = ex,
        };
    }
};

template <SomeSignal SignalT>
struct signal_awaiter final {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;
    using slot_ctor_type = awaiter_slot_ctor<value_type, error_type>;
    using connection_type = ConnectionFor<SignalT, slot_ctor_type>;

public:
    explicit signal_awaiter(SignalT&& signal) : state_{ std::in_place_type<SignalT>, std::move(signal) } {}

    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
        DEBUG_ASSERT(std::holds_alternative<SignalT>(state_));
        auto signal = std::get<SignalT>(std::move(state_));
        auto& executor = signal.get_executor();
        auto& a_connection = state_.template emplace<connection_type>(
            std::move(signal).subscribe(slot_ctor_type{ std::move(handle), maybe_result_, executor })
        );
        std::ignore = std::move(a_connection).emit();
    }
    meta::result<value_type, error_type> await_resume() {
        DEBUG_ASSERT(std::holds_alternative<connection_type>(state_));
        DEBUG_ASSERT(maybe_result_.has_value());
        return std::move(maybe_result_).value();
    }

private:
    std::variant<SignalT, connection_type> state_;
    meta::maybe<meta::result<value_type, error_type>> maybe_result_;
};

} // namespace detail

template <SomeSignal SignalT>
auto operator co_await(SignalT&& signal) {
    return detail::signal_awaiter<SignalT>{ std::move(signal) };
}

} // namespace sl::exec
