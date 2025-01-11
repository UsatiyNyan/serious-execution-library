//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/coro/detail.hpp"
#include "sl/exec/model/executor.hpp"

#include <sl/meta/lifetime/immovable.hpp>

#include <libassert/assert.hpp>

#include <coroutine>

namespace sl::exec {

template <typename T>
struct [[nodiscard]] async : meta::immovable {
    // vvv compiler hooks
    struct promise_type;
    // ^^^ compiler hooks

    using handle_type = std::coroutine_handle<promise_type>;
    struct awaiter;
    struct final_awaiter;

public:
    explicit async(handle_type handle) : handle_{ handle } {}
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
    : public task_node
    , public detail::promise_result_mixin<T> {
    using handle_type = std::coroutine_handle<promise_type>;

    // vvv task_node hooks
    void execute() noexcept override {
        handle_type handle = handle_type::from_promise(*this);
        handle.resume();
    }
    // TODO(@usatiynyan): make sure this actually works
    void cancel() noexcept override {
        handle_type handle = handle_type::from_promise(*this);
        handle.destroy();
    }
    // ^^^ task_node hooks

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
