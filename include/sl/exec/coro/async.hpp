//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/coro/detail.hpp"
#include "sl/exec/model/executor.hpp"

#include <coroutine>
#include <libassert/assert.hpp>
#include <sl/meta/lifetime/unique.hpp>

namespace sl::exec {

template <typename T>
class async;

template <typename T>
class async_promise
    : public task_node
    , public detail::promise_result_mixin<T> {
public:
    using handle_type = std::coroutine_handle<async_promise>;
    struct final_awaiter;

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
    std::coroutine_handle<> continuation{};
};

template <typename T>
struct async_promise<T>::final_awaiter {
    // vvv compiler hooks
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<> await_suspend(handle_type handle) noexcept {
        if (auto& self = handle.promise(); self.continuation) {
            return self.continuation;
        }
        return std::noop_coroutine();
    }
    void await_resume() noexcept {}
    // ^^^ compiler hooks
};

template <typename T>
class async : meta::unique {
public:
    // vvv compiler hooks
    using promise_type = async_promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    class awaiter;

    auto operator co_await() && noexcept { return awaiter{ handle_ }; }
    // ^^^ compiler hooks

    explicit async(handle_type handle) : handle_{ handle } {}
    ~async() {
        if (handle_) {
            DEBUG_ASSERT(handle_.done());
            handle_.destroy();
        }
    }

    async(async&& other) noexcept : handle_{ std::exchange(other.handle_, {}) } {}
    async& operator=(async&& other) noexcept { std::swap(handle_, other.handle_); }

    [[nodiscard]] auto release() && noexcept { return std::exchange(handle_, {}); }

private:
    handle_type handle_;
};

template <typename T>
class async<T>::awaiter {
public:
    explicit awaiter(handle_type handle) : handle_{ handle } {}

    // vvv compiler hooks
    bool await_ready() noexcept { return false; }
    template <typename U>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<async_promise<U>> continuation) {
        DEBUG_ASSERT(!handle_.done());
        handle_.promise().continuation = continuation;
        return handle_;
    }
    [[nodiscard]] auto await_resume() {
        DEBUG_ASSERT(handle_.done());
        return std::move(handle_.promise()).get_return_or_throw();
    }
    // ^^^ compiler hooks

private:
    handle_type handle_;
};

void coro_schedule(executor& executor, async<void> coro);

} // namespace sl::exec
