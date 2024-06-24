//
// Created by usatiynyan.
//

#pragma once

#include <coroutine>
#include <exception>
#include <libassert/assert.hpp>
#include <tl/optional.hpp>

namespace sl::exec {

template <typename T>
class generator {
public:
    // vvv compiler hooks
    class promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    // ^^^ compiler hooks

    explicit generator(handle_type handle) : handle_{ std::move(handle) } {}
    ~generator() { handle_.destroy(); }

    [[nodiscard]] tl::optional<T> next() {
        handle_.promise().assert_invariant();
        if (handle_.done()) {
            return tl::nullopt;
        }
        handle_.resume();
        return std::move(handle_.promise()).release();
    }

private:
    handle_type handle_;
};

template <typename T>
class generator<T>::promise_type {
public:
    // vvv compiler hooks
    auto get_return_object() { return generator{ handle_type::from_promise(*this) }; };

    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept { return std::suspend_always{}; }

    void unhandled_exception() { exception_ = std::current_exception(); }

    template <std::convertible_to<T> From>
    auto yield_value(From&& from) {
        value_ = std::forward<From>(from);
        return std::suspend_always{};
    }
    void return_void() {}
    // ^^^ compiler hooks

    void assert_invariant() { DEBUG_ASSERT(!value_.has_value() && !exception_); }
    tl::optional<T> release() && {
        if (exception_) [[unlikely]] {
            std::rethrow_exception(exception_);
        }

        tl::optional<T> extracted;
        value_.swap(extracted);
        return extracted;
    }

private:
    tl::optional<T> value_{};
    std::exception_ptr exception_{};
};

} // namespace sl::exec
