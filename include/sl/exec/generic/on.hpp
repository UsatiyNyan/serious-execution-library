//
// Created by usatiynyan.
// TODO: async_generator
//

#pragma once

#include "sl/exec/generic/async.hpp"
#include "sl/exec/generic/executor.hpp"

namespace sl::exec {
namespace detail {

struct on_awaiter {
    constexpr bool await_ready() noexcept { return false; }
    template <typename T>
    void await_suspend(std::coroutine_handle<async_promise<T>> handle) {
        auto& promise = handle.promise();
        // for any other awaitable to be able to schedule continuation on same executor
        promise.executor = &executor;
        executor.schedule(&promise);
    }
    constexpr void await_resume() noexcept {}

    generic_executor& executor;
};

} // namespace detail

inline auto on(generic_executor& executor) { return detail::on_awaiter{ .executor = executor }; }

} // namespace sl::exec
