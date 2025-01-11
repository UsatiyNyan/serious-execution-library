//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/coro/detail.hpp"

#include <sl/meta/lifetime/immovable.hpp>

#include <coroutine>

namespace sl::exec {

template <typename YieldT, typename ReturnT = void>
struct [[nodiscard]] async_gen : meta::immovable {
    // vvv compiler hooks
    struct promise_type;
    // ^^^ compiler hooks

    using handle_type = std::coroutine_handle<promise_type>;
    struct yield_awaiter;
    struct next_awaiter;

private:
    explicit async_gen(handle_type handle) : handle_{ handle } {}

public:
    ~async_gen() {
        if (handle_) {
            DEBUG_ASSERT(handle_.done());
            handle_.destroy();
        }
    }

    async_gen(async_gen&& other) noexcept : handle_{ std::exchange(other.handle_, {}) } {}

    [[nodiscard]] auto result() && {
        ASSERT(handle_.done());
        return std::move(handle_.promise()).get_return();
    }
    [[nodiscard]] auto result_or_throw() && {
        ASSERT(handle_.done());
        return std::move(handle_.promise()).get_return_or_throw();
    }

    // vvv compiler hooks
    auto operator co_await() & noexcept { return next_awaiter{ handle_ }; }
    // ^^^ compiler hooks

private:
    handle_type handle_;
};

template <typename YieldT, typename ReturnT>
struct async_gen<YieldT, ReturnT>::promise_type : detail::promise_result_mixin<ReturnT> {
    // vvv compiler hooks
    auto get_return_object() { return async_gen{ handle_type::from_promise(*this) }; };

    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept {
        DEBUG_ASSERT(!maybe_yield_.has_value());
        return yield_awaiter{ consumer };
    }

    template <std::convertible_to<YieldT> From>
    auto yield_value(From&& from) {
        maybe_yield_.emplace(std::forward<From>(from));
        return yield_awaiter{ consumer };
    }
    // ^^^ compiler hooks

    [[nodiscard]] meta::maybe<YieldT> get_yield() & noexcept {
        meta::maybe<YieldT> extracted{ std::move(maybe_yield_) };
        maybe_yield_.reset();
        return extracted;
    }
    [[nodiscard]] meta::maybe<YieldT> get_yield_or_throw() & {
        detail::promise_result_mixin<ReturnT>::try_rethrow();
        return get_yield();
    }

private:
    meta::maybe<YieldT> maybe_yield_;

public:
    std::coroutine_handle<> consumer;
};

template <typename YieldT, typename ReturnT>
struct async_gen<YieldT, ReturnT>::yield_awaiter {
    explicit yield_awaiter(std::coroutine_handle<> consumer) : consumer_{ consumer } {}

    // vvv compiler hooks
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* producer */) noexcept { return consumer_; }
    void await_resume() noexcept {}
    // ^^^ compiler hooks

private:
    std::coroutine_handle<> consumer_;
};

template <typename YieldT, typename ReturnT>
struct async_gen<YieldT, ReturnT>::next_awaiter {
    explicit next_awaiter(handle_type producer) : producer_{ producer }, producer_promise_{ producer_.promise() } {}

    // vvv compiler hooks
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> consumer) noexcept {
        producer_promise_.consumer = consumer;
        return producer_;
    }
    [[nodiscard]] meta::maybe<YieldT> await_resume() { return producer_promise_.get_yield_or_throw(); }
    // ^^^ compiler hooks

private:
    handle_type producer_;
    promise_type& producer_promise_;
};

} // namespace sl::exec
