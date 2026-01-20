//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/coro/detail.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/traits/unique.hpp>

#include <coroutine>

namespace sl::exec {

template <typename T>
struct coroutine : meta::immovable {
    // vvv compiler hooks
    struct promise_type;
    // ^^^ compiler hooks

    using handle_type = std::coroutine_handle<promise_type>;
    struct final_awaiter;
    struct awaiter;

private:
    explicit coroutine(handle_type handle) : handle_{ handle } {}

public:
    ~coroutine() {
        if (handle_) {
            handle_.destroy();
        }
    }

    coroutine(coroutine&& other) noexcept : handle_{ std::exchange(other.handle_, {}) } {}

    void resume() { handle_.resume(); }

    [[nodiscard]] auto result() && {
        ASSERT(handle_.done());
        return std::move(handle_.promise()).get_return();
    }
    [[nodiscard]] auto result_or_throw() && {
        ASSERT(handle_.done());
        return std::move(handle_.promise()).get_return_or_throw();
    }

    // vvv compiler hooks
    auto operator co_await() && noexcept { return awaiter{ handle_ }; }
    // ^^^ compiler hooks

private:
    handle_type handle_;
};

template <typename T>
struct coroutine<T>::promise_type : detail::promise_result_mixin<T> {
    // vvv compiler hooks
    auto get_return_object() { return coroutine{ handle_type::from_promise(*this) }; };
    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept { return final_awaiter{}; }
    // ^^^ compiler hooks

public:
    std::coroutine_handle<> continuation{};
};

template <typename T>
struct coroutine<T>::final_awaiter {
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
struct coroutine<T>::awaiter {
    explicit awaiter(handle_type handle) : handle_{ handle } {}

    // vvv compiler hooks
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
        ASSERT(!handle_.done());
        handle_.promise().continuation = continuation;
        return handle_;
    }
    [[nodiscard]] auto await_resume() {
        ASSERT(handle_.done());
        return std::move(handle_.promise()).get_return_or_throw();
    }
    // ^^^ compiler hooks

private:
    handle_type handle_;
};

} // namespace sl::exec
