//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/coro/detail.hpp"
#include "sl/exec/model/executor.hpp"

#include <sl/meta/traits/unique.hpp>

#include <sl/meta/assert.hpp>

#include <coroutine>

namespace sl::exec {
namespace detail {

template <typename, typename, typename>
struct async_task_mixin {};

template <typename promise_type, typename handle_type>
struct async_task_mixin<void, promise_type, handle_type> : task_node {
    // vvv task_node hooks
    void execute() noexcept override {
        auto* promise_ptr = static_cast<promise_type*>(this);
        handle_type handle = handle_type::from_promise(*promise_ptr);
        handle.resume();
    }
    void cancel() noexcept override {
        auto* promise_ptr = static_cast<promise_type*>(this);
        handle_type handle = handle_type::from_promise(*promise_ptr);
        handle.destroy();
    }
    // ^^^ task_node hooks
};

} // namespace detail

template <typename T>
struct [[nodiscard]] async : meta::immovable {
    // vvv compiler hooks
    struct promise_type;
    // ^^^ compiler hooks

    using handle_type = std::coroutine_handle<promise_type>;
    struct awaiter;
    struct final_awaiter;

private:
    explicit async(handle_type handle) : handle_{ handle } {}

public:
    ~async() {
        if (handle_) {
            DEBUG_ASSERT(handle_.done());
            handle_.destroy();
        }
    }

    async(async&& other) noexcept : handle_{ std::exchange(other.handle_, {}) } {}
    [[nodiscard]] auto release() && noexcept { return std::exchange(handle_, {}); }

    // vvv compiler hooks
    auto operator co_await() && noexcept { return awaiter{ handle_ }; }
    // ^^^ compiler hooks

private:
    handle_type handle_;
};

template <typename T>
struct async<T>::promise_type
    : detail::promise_result_mixin<T>
    , detail::async_task_mixin<T, promise_type, handle_type> {

    // vvv compiler hooks
    auto get_return_object() { return async{ handle_type::from_promise(*this) }; };
    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept { return final_awaiter{}; }
    // ^^^ compiler hooks

public:
    std::coroutine_handle<> continuation = std::noop_coroutine();
};

template <typename T>
struct async<T>::awaiter {
    explicit awaiter(handle_type handle) : handle_{ handle }, promise_{ handle.promise() } {}

    // vvv compiler hooks
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) {
        DEBUG_ASSERT(!handle_.done());
        promise_.continuation = continuation;
        return handle_;
    }
    [[nodiscard]] auto await_resume() {
        DEBUG_ASSERT(handle_.done());
        return std::move(promise_).get_return_or_throw();
    }
    // ^^^ compiler hooks

private:
    handle_type handle_;
    promise_type& promise_;
};

template <typename T>
struct async<T>::final_awaiter {
    // vvv compiler hooks
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<> await_suspend(handle_type handle) noexcept { return handle.promise().continuation; }
    void await_resume() noexcept {}
    // ^^^ compiler hooks
};

void coro_schedule(executor& executor, async<void> coro);

} // namespace sl::exec
