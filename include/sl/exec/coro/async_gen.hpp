//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/lifetime/immovable.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>

#include <libassert/assert.hpp>

#include <coroutine>
#include <exception>

namespace sl::exec {

template <typename T>
struct [[nodiscard]] async_gen : meta::immovable {
    // vvv compiler hooks
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    // ^^^ compiler hooks

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

    auto operator co_await() & noexcept { return next_awaiter{ handle_ }; }

private:
    handle_type handle_;
};

template <typename T>
struct async_gen<T>::promise_type {
    using yield_type = meta::result<T, std::exception_ptr>;

public:
    // vvv compiler hooks
    auto get_return_object() { return async_gen<T>{ handle_type::from_promise(*this) }; };

    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept {
        DEBUG_ASSERT(!maybe_yield_.has_value());
        return yield_awaiter{ consumer_ };
    }

    void unhandled_exception() { maybe_yield_.emplace(tl::unexpect, std::current_exception()); }

    template <std::convertible_to<T> From>
    auto yield_value(From&& from) {
        maybe_yield_.emplace(tl::in_place, std::forward<From>(from));
        return yield_awaiter{ consumer_ };
    }
    void return_void() {}
    // ^^^ compiler hooks

    [[nodiscard]] meta::maybe<yield_type> get_yield() && noexcept {
        meta::maybe<yield_type> extracted;
        maybe_yield_.swap(extracted);
        return std::move(extracted);
    }
    [[nodiscard]] meta::maybe<T> get_yield_or_throw() && {
        return std::move(*this).get_yield().map([](yield_type yield_value) {
            if (!yield_value.has_value()) [[unlikely]] {
                std::rethrow_exception(yield_value.error());
            }
            return std::move(yield_value).value();
        });
    }

private:
    meta::maybe<yield_type> maybe_yield_;
    std::coroutine_handle<> consumer_;
};

template <typename T>
struct async_gen<T>::yield_awaiter {
    explicit yield_awaiter(std::coroutine_handle<> consumer) : consumer_{ consumer } {}

    // vvv compiler hooks
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* producer */) noexcept { return consumer_; }
    void await_resume() noexcept {}
    // ^^^ compiler hooks

private:
    std::coroutine_handle<> consumer_;
};

template <typename T>
struct async_gen<T>::next_awaiter {
    explicit next_awaiter(handle_type producer) : producer_{ producer }, self_{ producer_.promise() } {}

    // vvv compiler hooks
    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> consumer) noexcept {
        self_.consumer_ = consumer;
        return producer_;
    }
    meta::maybe<T> await_resume() { return self_.get_yield_or_throw(); }
    // ^^^ compiler hooks

private:
    handle_type producer_;
    promise_type& self_;
};

} // namespace sl::exec
