//
// Created by usatiynyan.
// TODO: resume on executor!!!
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"
#include "sl/exec/model/concept.hpp"

#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>

#include <coroutine>
#include <variant>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT>
struct awaiter_slot : slot<ValueT, ErrorT> {
    struct awaiter_task : task_node {
        explicit awaiter_task(std::coroutine_handle<> handle) : handle_{ std::move(handle) } {}

        void execute() noexcept override { handle_.resume(); }
        void cancel() noexcept override { handle_.destroy(); }

    private:
        std::coroutine_handle<> handle_;
    };

public:
    awaiter_slot(
        std::coroutine_handle<> handle,
        meta::maybe<meta::result<ValueT, ErrorT>>& maybe_result,
        executor& executor
    )
        : task_{ std::move(handle) }, maybe_result_{ maybe_result }, executor_{ executor } {}

    void set_value(ValueT&& value) & override {
        maybe_result_.emplace(tl::in_place, std::move(value));
        executor_.schedule(&task_);
    }
    void set_error(ErrorT&& error) & override {
        maybe_result_.emplace(tl::unexpect, std::move(error));
        executor_.schedule(&task_);
    }
    void cancel() & override { task_.cancel(); }

private:
    awaiter_task task_;
    meta::maybe<meta::result<ValueT, ErrorT>>& maybe_result_;
    executor& executor_;
};

template <Signal SignalT>
struct signal_awaiter {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

private:
    using connection_type = subscribe_connection<SignalT, awaiter_slot<value_type, error_type>>;

public:
    explicit signal_awaiter(SignalT&& signal) : state_{ std::in_place_type<SignalT>, std::move(signal) } {}

    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
        DEBUG_ASSERT(std::holds_alternative<SignalT>(state_));
        auto signal = std::get<SignalT>(std::move(state_));
        auto& executor = signal.get_executor();
        auto& connection = state_.template emplace<connection_type>(
            /* .signal = */ std::move(signal),
            /* .slot = */ awaiter_slot<value_type, error_type>{
                /* .handle = */ std::move(handle),
                /* .maybe_result = */ maybe_result_,
                /* .executor = */ executor,
            }
        );
        std::move(connection).emit();
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

template <Signal SignalT>
auto operator co_await(SignalT&& signal) {
    return detail::signal_awaiter<SignalT>{ std::move(signal) };
}

} // namespace sl::exec
