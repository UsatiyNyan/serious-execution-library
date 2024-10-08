//
// Created by usatiynyan.
//

#pragma once

#include "sl/coro/detail.hpp"

#include <coroutine>
#include <libassert/assert.hpp>

namespace sl::coro {

template <typename T>
class task {
public:
    // vvv compiler hooks
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    struct final_awaiter;
    class awaiter;

    auto operator co_await() && noexcept { return awaiter{ handle_ }; }
    // ^^^ compiler hooks

private:
    explicit task(handle_type handle) : handle_{ handle } {}

public:
    ~task() {
        if (handle_) {
            handle_.destroy();
        }
    }

    task(const task&) = delete;
    task& operator=(const task&) = delete;
    task(task&& other) noexcept : handle_{ std::exchange(other.handle_, {}) } {}
    task& operator=(task&& other) noexcept { std::swap(handle_, other.handle_); }

    void start() { handle_.resume(); }

    [[nodiscard]] auto result() && {
        ASSUME(handle_.done());
        return std::move(handle_.promise()).get_return();
    }
    [[nodiscard]] auto result_or_throw() && {
        ASSUME(handle_.done());
        return std::move(handle_.promise()).get_return_or_throw();
    }

private:
    handle_type handle_;
};

template <typename T>
struct task<T>::promise_type : public detail::promise_result_mixin<T> {
    // vvv compiler hooks
    auto get_return_object() { return task{ handle_type::from_promise(*this) }; };
    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept { return final_awaiter{}; }
    // ^^^ compiler hooks

public:
    std::coroutine_handle<> continuation{};
};

template <typename T>
struct task<T>::final_awaiter {
    // vvv compiler hooks
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<> await_suspend(handle_type handle) noexcept {
        if (auto continuation = handle.promise().continuation) {
            return continuation;
        }
        return std::noop_coroutine();
    }
    void await_resume() noexcept {}
    // ^^^ compiler hooks
};

template <typename T>
class task<T>::awaiter {
public:
    explicit awaiter(handle_type handle) : handle_{ handle } {}

    // vvv compiler hooks
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
        ASSUME(!handle_.done());
        handle_.promise().continuation = continuation;
        return handle_;
    }
    [[nodiscard]] auto await_resume() {
        ASSUME(handle_.done());
        return std::move(handle_.promise()).get_return_or_throw();
    }
    // ^^^ compiler hooks

private:
    handle_type handle_;
};

} // namespace sl::coro
